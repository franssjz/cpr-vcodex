# Changelog

This changelog starts at `1.2.0.24`, the point where CPR-vCodex began tracking release changes in this file.

| Version | Changes |
|---|---|
| `1.2.0.29` | - Synced the fork with upstream through `e28918b`, including OPDS/copyparty fixes, KOSync improvements, keyboard updates, web UI tweaks, and minor typo cleanups.<br>- Fixed the `Lyra vCodex` theme so opening the Wi-Fi password keyboard no longer freezes when connecting to protected networks. |
| `1.2.0.28` | - Tuned `Text Darkness` again so the first black-and-white page render in `Normal` matches the final anti-aliased result more closely.<br>- Kept `Dark` and `Extra Dark` bold while reducing the visible "first bold, then softer" jump when turning pages. |
| `1.2.0.27` | - Rebalanced `Text Darkness` so `Normal`, `Dark`, and `Extra Dark` all render noticeably bolder.<br>- Improved the perceived text weight without adding extra rendering passes or slowing the reader down. |
| `1.2.0.26` | - Hardened book completion tracking so achievements still register when leaving from explicit `End of book` states.<br>- Applied the completion fix consistently across EPUB, TXT, and XTC readers. |
| `1.2.0.25` | - Added PNG sleep image compatibility while keeping BMP sleep images unchanged.<br>- PNG sleep images now work as transparent overlays on top of the last visible screen.<br>- Sleep preview now recognizes and previews both BMP and PNG files. |
| `1.2.0.24` | - Reviewed and corrected all 23 bundled UI languages.<br>- Synced the fork with upstream through `64f5ef0`, including keyboard, XTC, parser, docs, font, and i18n updates.<br>- Fixed runtime UI font replacement so Vietnamese glyphs render correctly.<br>- Normalized translation newlines and multiline text handling again after the YAML/i18n regressions. |
