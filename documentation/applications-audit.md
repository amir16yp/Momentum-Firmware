# Applications C audit

Scope: 857 tracked `.c` files under `applications/`, excluding the
`applications/external` Git submodule. Searches covered allocation/reallocation,
string operations, copies, buffer indexing, rendering loops, and floating-point
work. Manual review focused on the resulting candidates and their callers. This
is a source audit with focused host validation, not an exhaustive proof of every
application or a device benchmark.

## Changes

| Area | Finding and fix |
| --- | --- |
| GUI scrolling text | Draw fitting text directly, eliminating its temporary string allocation and copy. Oversized text keeps the existing scrolling path. |
| Button menu | Draw fitting labels directly; visit only the active page's six entries. |
| Variable-item list | Visit only the visible three or four entries, hoisting shared layout calculations out of the loop. |
| Text input | Reserve the extra byte needed to insert the cursor; initialize the empty buffer; avoid truncating lengths to eight bits; remove a redundant length scan. |
| JavaScript console | Replace repeated whole-input length scans with a terminator check. Apply line bounds to non-ASCII output as well as ASCII output. |
| JavaScript icons | Validate dimensions, bitmap extent, allocation arithmetic, and file length before allocation. Avoid incrementing a null search result. |
| HID push-to-talk menu | Preserve callback fields when copying items. Ensure error paths release the view model and preserve the old list pointer on a failed reallocation. The firmware allocator normally fails fatally; the latter path is additionally exercised with a failure-injecting host allocator. |
| Sonicare NFC parser | Use constant color strings and integer time formatting, removing a temporary allocation and floating-point formatting. |
| Moscow social-card parser | Replace eight `pow()` calls and redundant nibble reconstruction with integer decimal accumulation. |
| Neofetch | Bound the hostname delimiter and handle failed, zero-sized, or inconsistent storage statistics without division by zero or uninitialized reads. |

The menu changes add no persistent buffers. Fitting text requires no temporary
allocation in the changed helpers. Actual CPU time, heap fragmentation, and
firmware size differences have not been measured.

## Validation

From `scripts/tests`, run:

```powershell
$env:HOT_PATH_ASAN = '1'
python -m unittest test_applications_audit test_hot_path_optimization -v
```

The new host tests compile production functions with warnings treated as errors.
They cover UTF-8 wrapping, cursor positions through 300 characters, visible menu
ranges through 300 entries, short-label allocations, scrolling behavior, malformed
image headers, HID error-path unlocking and callbacks, missing storage, all 65,536
Sonicare counter values, and 100,000 decimal-conversion inputs. Existing hot-path
checks cover submenu rendering, multiline drawing, and stream operations.

Host GUI/storage stubs do not validate actual font metrics, device rendering,
thread scheduling, or hardware behavior. A complete ARM build and device smoke
test remain necessary: this checkout has neither the ARM toolchain nor populated
dependency submodules. No submodule contents or Gitlinks were changed.
