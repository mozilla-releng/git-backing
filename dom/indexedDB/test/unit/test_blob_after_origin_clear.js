/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/* exported testSteps */

// Test that sending a blob to a database which has already been invalidated
// by clearing its origin doesn't trip assertions in the parent (bug 2090070).

async function testBlobAfterOriginClear() {
  const principal = getPrincipal("https://example.com");

  const db = await new Promise((resolve, reject) => {
    const request = indexedDB.openForPrincipal(principal, "test", 1);
    request.onupgradeneeded = e => e.target.result.createObjectStore("store");
    request.onsuccess = e => resolve(e.target.result);
    request.onerror = reject;
  });

  // Start clearing the origin and block the main thread, so the parent
  // invalidates the database and its file manager before the child processes
  // the invalidation.
  const clearRequest = Services.qms.clearStoragesForPrincipal(principal);
  const start = Date.now();
  while (Date.now() - start < 2000) {
    // Busy wait.
  }

  // The child doesn't know about the invalidation yet, so this sends the blob
  // to the invalidated database in the parent.
  const tx = db.transaction("store", "readwrite");
  tx.objectStore("store").add(new Blob([new ArrayBuffer(1024)]), 1);

  await new Promise(resolve => {
    clearRequest.callback = resolve;
  });
  Assert.equal(clearRequest.resultCode, Cr.NS_OK, "Clearing succeeded");

  // Messages on the same PBackground channel are processed in order, so once
  // this round trip completes the parent has processed the blob.
  await new Promise(resolve =>
    Services.qms.getUsageForPrincipal(principal, resolve)
  );

  db.close();
}

async function testSteps() {
  add_task(testBlobAfterOriginClear);
}
