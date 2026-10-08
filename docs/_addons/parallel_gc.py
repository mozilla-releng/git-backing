import gc

_state = {"disabled": False}


def _parallel_read_allowed(app):
    return app.parallel > 1 and all(
        getattr(ext, "parallel_read_safe", None) for ext in app.extensions.values()
    )


def _disable_gc(app, env, docnames):
    if _state["disabled"] or not _parallel_read_allowed(app):
        return
    gc.disable()
    _state["disabled"] = True


def _enable_gc(app, exception):
    if _state["disabled"]:
        gc.enable()
        _state["disabled"] = False


def setup(app):
    app.connect("env-before-read-docs", _disable_gc)
    app.connect("build-finished", _enable_gc)
    return {"parallel_read_safe": True, "parallel_write_safe": True}
