# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## Confirmation prompts shown before deleting a folder.

folder-delete-title = Delete Folder

# Button label for the folder deletion prompt. The ampersand marks the access
# key, as required by the prompt service.
folder-delete-button = &Delete Folder

# Variables:
#   $name (String) - Name of the folder being deleted.
folder-delete-confirm = Are you sure you want to delete the folder '{ $name }'?

# Shown instead of folder-delete-confirm when the folder cannot be moved to a
# trash folder, and so will be destroyed straight away.
# Variables:
#   $name (String) - Name of the folder being deleted.
folder-delete-no-trash-confirm = Deleting this folder is not undoable and will delete all of the messages it contains, and its sub-folders. Are you sure you still want to delete the folder '{ $name }'?

# Variables:
#   $name (String) - Name of the folder being deleted.
folder-delete-filter-confirm = Deleting the folder '{ $name }' will disable its associated filter(s). Are you sure you want to delete the folder?
