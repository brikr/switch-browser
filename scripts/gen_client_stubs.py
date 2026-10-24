"""Generate default implementations of the WKC host interfaces from the WKC headers.

For each interface class, emits `Stub<Class>` that overrides every pure virtual method with a
no-op that logs the call (with NXB_TRACE=1) and returns a value-initialised result. Hand-written
host code subclasses these and overrides only what it needs.

Usage:
  python3 -I scripts/gen_client_stubs.py --wkc <WKC_0.30.0 dir> --out loader/src/host/generated/stub_clients.h
"""
import argparse
import pathlib
import re

# (header relative to WKC/WebKit/WKC, class name)
INTERFACES = [
    ("helpers/ChromeClientIf.h", "ChromeClientIf"),
    ("helpers/ContextMenuClientIf.h", "ContextMenuClientIf"),
    ("helpers/DragClientIf.h", "DragClientIf"),
    ("helpers/EditorClientIf.h", "EditorClientIf"),
    ("helpers/FrameLoaderClientIf.h", "FrameLoaderClientIf"),
    ("helpers/InspectorClientIf.h", "InspectorClientIf"),
    ("helpers/DropDownListClientIf.h", "DropDownListClientIf"),
    ("helpers/GeolocationClientIf.h", "GeolocationClientIf"),
    ("helpers/DeviceMotionClientIf.h", "DeviceMotionClientIf"),
    ("helpers/DeviceOrientationClientIf.h", "DeviceOrientationClientIf"),
    ("helpers/BackForwardClientIf.h", "BackForwardClientIf"),
    ("helpers/BatteryClientIf.h", "BatteryClientIf"),
    ("helpers/NavigatorContentUtilsClientIf.h", "NavigatorContentUtilsClientIf"),
    ("helpers/ProgressTrackerClientIf.h", "ProgressTrackerClientIf"),
    ("helpers/WebNfcClientIf.h", "WebNfcClientIf"),
    ("helpers/StorageAreaIf.h", "StorageAreaIf"),
    ("helpers/VibrationClientIf.h", "VibrationClientIf"),
]


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    return re.sub(r"//[^\n]*", " ", src)


def class_body(src, name):
    m = re.search(r"\bclass\s+(?:WKC_API\s+)?%s\b[^;{]*\{" % re.escape(name), src)
    if not m:
        raise SystemExit(f"class {name} not found")
    depth, i = 1, m.end()
    while depth:
        c = src[i]
        depth += c == "{"
        depth -= c == "}"
        i += 1
    return src[m.end():i - 1]


# Feature macros as configured in the shipped core_0 engine (see loader/src/host/wkc_api.h).
DEFINED = {"USE_WKC_CAIRO"}
UNDEFINED = {"WKC_ENABLE_CUSTOMJS"}


def preprocess(body):
    """Applies #ifdef/#ifndef/#else/#endif for known macros; other #if blocks are kept (fatal if
    they contain methods, since we can't know which branch the engine was built with)."""
    out, stack = [], []  # stack of (active, known)
    for line in body.splitlines():
        s = line.strip()
        m = re.match(r"#\s*(ifdef|ifndef|if|elif|else|endif)\b\s*(\w*)", s)
        if not m:
            if all(a for a, _ in stack):
                if any(not k for _, k in stack) and "virtual" in s:
                    raise SystemExit(f"method under unknown #if: {s}")
                out.append(line)
            continue
        kind, macro = m.groups()
        if kind in ("ifdef", "ifndef"):
            known = macro in DEFINED or macro in UNDEFINED
            active = (macro in DEFINED) == (kind == "ifdef") if known else True
            stack.append((active, known))
        elif kind == "if":
            stack.append((True, False))
        elif kind in ("else", "elif"):
            active, known = stack.pop()
            stack.append((not active if known else True, known))
        elif kind == "endif":
            stack.pop()
    return "\n".join(out)


def pure_virtuals(body):
    body = preprocess(body)
    for stmt in re.split(r"[;{}]", body):
        stmt = " ".join(stmt.split())
        m = re.search(r"\bvirtual\s+(.*\))\s*(const)?\s*=\s*0$", stmt)
        if not m:
            continue
        decl, const = m.group(1), m.group(2) or ""
        name_m = re.search(r"(~?\w+)\s*\((.*)\)$", decl)
        name = name_m.group(1)
        if name.startswith("~"):
            continue  # pure virtual destructors are defined by the engine
        ret = decl[: name_m.start()].strip()
        yield ret, name, name_m.group(2), const


def emit_method(cls, ret, name, args, const):
    sig = f"{ret} {name}({args}){' const' if const else ''} override"
    true_default = any(cls == c and name.startswith(prefix) for c, prefix in TRUE_DEFAULTS)
    noop = true_default or cls in NOOP_OK_CLASSES or f"{cls}::{name}" in NOOP_OK
    macro = "NXB_STUB_NOOP" if noop else "NXB_STUB_CALL"
    log = f'{macro}("{cls}::{name}");'
    if ret == "void":
        return f"    {sig} {{ {log} }}"
    value = special_returns().get(ret.replace("WKC::", ""), "{}")
    if ret == "bool" and any(cls == c and name.startswith(prefix) for c, prefix in TRUE_DEFAULTS):
        value = "true"
    return f"    {sig} {{ {log} return {value}; }}"


# Boolean methods that must default to true for the engine to work: the editor's "should..."
# permission checks (false blocks all text editing). Other interfaces' should* stay false
# (e.g. ChromeClientIf::shouldInterruptJavaScript).
TRUE_DEFAULTS = [("EditorClientIf", "should")]

# Stubs whose no-op/default is the right behaviour for this host: they're counted but not logged
# (see stub_report.h). Everything else is reported on its first call, so add a method here only
# after checking that ignoring it can't change what a page sees.
NOOP_OK_CLASSES = {
    "ProgressTrackerClientIf",  # load progress notifications; the UI uses load start/finish instead
}
NOOP_OK = {
    # Load notifications. The engine does the work itself; these only inform the embedder.
    "FrameLoaderClientIf::assignIdentifierToInitialRequest",
    "FrameLoaderClientIf::cancelPolicyCheck",
    "FrameLoaderClientIf::committedLoad",
    "FrameLoaderClientIf::didChangeTitle",
    "FrameLoaderClientIf::dispatchDidClearWindowObjectInWorld",
    "FrameLoaderClientIf::dispatchDidDispatchOnloadEvents",
    "FrameLoaderClientIf::dispatchDidFinishDocumentLoad",
    "FrameLoaderClientIf::dispatchDidFinishLoading",
    "FrameLoaderClientIf::dispatchDidLayout",
    "FrameLoaderClientIf::dispatchDidReachLayoutMilestone",
    "FrameLoaderClientIf::dispatchDidReceiveContentLength",
    "FrameLoaderClientIf::dispatchDidReceiveResponse",
    "FrameLoaderClientIf::dispatchDidReceiveServerRedirectForProvisionalLoad",
    "FrameLoaderClientIf::dispatchGlobalObjectAvailable",
    "FrameLoaderClientIf::dispatchWillClose",
    "FrameLoaderClientIf::dispatchWillSendRequest",  # the embedder may rewrite requests; we don't
    "FrameLoaderClientIf::finishedLoading",
    "FrameLoaderClientIf::forceLayoutForNonHTML",
    "FrameLoaderClientIf::frameLoadCompleted",
    "FrameLoaderClientIf::makeRepresentation",
    "FrameLoaderClientIf::prepareForDataSourceReplacement",
    "FrameLoaderClientIf::provisionalLoadStarted",
    "FrameLoaderClientIf::representationExistsForURLScheme",  # false: no custom URL scheme views
    "FrameLoaderClientIf::setCopiesOnScroll",
    "FrameLoaderClientIf::setMainFrameDocumentReady",
    "FrameLoaderClientIf::setTitle",
    "FrameLoaderClientIf::transitionToCommittedForNewPage",
    "FrameLoaderClientIf::updateGlobalHistory",
    "FrameLoaderClientIf::updateGlobalHistoryRedirectLinks",
    "FrameLoaderClientIf::willChangeTitle",
    "FrameLoaderClientIf::didChangeScrollOffset",
    "FrameLoaderClientIf::didFinishLoad",
    "FrameLoaderClientIf::didRestoreFromHTTPCache",
    "FrameLoaderClientIf::dispatchDidLoadResourceFromMemoryCache",
    "FrameLoaderClientIf::dispatchDidNavigateWithinPage",  # also reported as push/pop/location change
    # Per-item view state for the embedder (zoom etc.); WebCore restores scroll positions itself.
    "FrameLoaderClientIf::saveViewStateToItem",
    "FrameLoaderClientIf::restoreViewState",
    "FrameLoaderClientIf::shouldUseCredentialStorage",  # false; only matters for HTTP auth (no UI yet)
    # View notifications: no zoom, scrollbars or native focus ring in this host.
    "ChromeClientIf::allowedCompositingTriggers",  # 0: no accelerated compositing (layer callbacks unset)
    "ChromeClientIf::contentsSizeChanged",
    "ChromeClientIf::dispatchApplyOverflowToViewport",
    "ChromeClientIf::dispatchViewportDataDidChange",
    "ChromeClientIf::dispatchWillSetHasFocusVisibleForFocusedElement",
    "ChromeClientIf::elementDidBlur",
    "ChromeClientIf::elementDidFocus",
    "ChromeClientIf::elementDidRefocus",
    "ChromeClientIf::shouldDispatchFakeMouseMoveEvents",  # false: hover isn't re-evaluated after scrolling
    "ChromeClientIf::focusedFrameChanged",
    "ChromeClientIf::focusedNodeChanged",
    "ChromeClientIf::scheduleRenderingUpdate",
    "ChromeClientIf::scrollContainingScrollViewsToRevealRect",
    "ChromeClientIf::shouldInterruptJavaScript",  # false: never interrupt long-running scripts
    "ChromeClientIf::shouldPaintEntireContents",
    "ChromeClientIf::triggerRenderingUpdate",
    "ChromeClientIf::wheelEventHandlersChanged",
    # Editing state: no undo menu, spell checker or IME in this host.
    "EditorClientIf::clearUndoRedoOperations",
    "EditorClientIf::doTextFieldCommandFromEvent",  # false: the engine handles the key itself
    "EditorClientIf::respondToChangedContents",
    "EditorClientIf::textDidChangeInTextField",
    "EditorClientIf::textFieldDidEndEditing",
    "EditorClientIf::isContinuousSpellCheckingEnabled",
    "EditorClientIf::respondToChangedSelection",
    "EditorClientIf::setInputMethodState",
    "EditorClientIf::textFieldDidBeginEditing",
    "EditorClientIf::willSetInputMethodState",
}

# Return types without a usable default constructor (set per WKC version in main()).
_special = {}


def special_returns():
    return _special


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wkc", required=True, help="WKC_0.30.0 directory from the OSS package")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    root = pathlib.Path(args.wkc) / "WebKit" / "WKC"
    resource_error = (root / "helpers" / "WKCResourceError.h").read_text(errors="replace")
    if "ResourceErrorType type, ResourceHandle*" in resource_error:  # 3.30.3
        _special["ResourceError"] = ("ResourceError(String(), 0, String(), String(), "
                                     "ResourceErrorType::ResourceErrorGeneral, nullptr)")
    else:  # 0.30.0
        _special["ResourceError"] = "ResourceError(String(), 0, String(), String(), nullptr)"

    out = [
        "// Generated by scripts/gen_client_stubs.py from the WKC 0.30.0 headers. Do not edit.",
        "#pragma once",
        "",
        '#include "host/wkc_api.h"',
        '#include "stub_report.h"',
        "",
        "#ifndef NXB_STUB_CALL",
        "#define NXB_STUB_CALL(name) ::nxb::report_stub(name)",
        "#endif",
        "#ifndef NXB_STUB_NOOP",
        "#define NXB_STUB_NOOP(name) ::nxb::report_stub(name, true)",
        "#endif",
        "",
        "// Inside namespace WKC so the headers' unqualified type names resolve.",
        "namespace WKC {",
        "",
    ]
    total = 0
    for header, cls in INTERFACES:
        if not (root / header).exists():
            continue  # interface not in this WKC version (e.g. Battery in 3.30.3)
        src = strip_comments((root / header).read_text(encoding="utf-8", errors="replace"))
        methods = list(pure_virtuals(class_body(src, cls)))
        total += len(methods)
        out.append(f"class Stub{cls} : public {cls} {{")
        out.append("public:")
        out += [emit_method(cls, *m) for m in methods]
        out.append("};")
        out.append("")
    out.append("}  // namespace WKC")
    path = pathlib.Path(args.out)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(out) + "\n")
    print(f"{path}: {len(INTERFACES)} classes, {total} methods")


if __name__ == "__main__":
    main()
