from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(path: str, needle: str) -> None:
    content = read(path)
    if needle not in content:
        raise AssertionError(f"{path} is missing: {needle}")


def main() -> None:
    required_files = [
        "src/components/themes/lyra/LyraCarouselTheme.h",
        "src/components/themes/lyra/LyraCarouselTheme.cpp",
    ]
    for path in required_files:
        if not (ROOT / path).is_file():
            raise AssertionError(f"{path} is missing")

    require("src/CrossPointSettings.h", "LYRA_CAROUSEL")
    require("src/SettingsList.cpp", "StrId::STR_THEME_LYRA_CAROUSEL")
    require("src/activities/settings/SettingsActivity.cpp", "StrId::STR_THEME_LYRA_CAROUSEL")
    require("src/network/CrossPointWebServer.cpp", "StrId::STR_THEME_LYRA_CAROUSEL")
    require("lib/I18n/I18nKeys.h", "STR_THEME_LYRA_CAROUSEL")
    require("lib/I18n/translations/english.yaml", "STR_THEME_LYRA_CAROUSEL: \"Lyra Carousel\"")
    require("src/components/UITheme.cpp", "#include \"components/themes/lyra/LyraCarouselTheme.h\"")
    require("src/components/UITheme.cpp", "std::make_unique<LyraCarouselTheme>()")
    require("src/components/UITheme.h", "getCoverThumbPath(std::string coverBmpPath, int coverWidth, int coverHeight)")
    require("src/components/UITheme.cpp", "std::string UITheme::getCoverThumbPath(std::string coverBmpPath, int coverWidth, int coverHeight)")
    require("lib/Epub/Epub.h", "generateThumbBmp(int width, int height)")
    require("lib/Epub/Epub.cpp", "bool Epub::generateThumbBmp(int width, int height) const")
    require("lib/Epub/Epub.cpp", "getThumbBmpPath(height)")
    require("lib/Xtc/Xtc.h", "generateThumbBmp(int width, int height)")
    require("lib/Xtc/Xtc.cpp", "bool Xtc::generateThumbBmp(int width, int height) const")
    require("lib/Xtc/Xtc.cpp", "getThumbBmpPath(height)")
    require("src/components/themes/BaseTheme.h", "drawCarouselBorder")
    require("src/components/themes/lyra/LyraCarouselTheme.h", "setPreRenderIndex")
    require("src/components/themes/lyra/LyraCarouselTheme.cpp", "void LyraCarouselTheme::setPreRenderIndex")
    require("src/components/themes/lyra/LyraCarouselTheme.h", "drawCarouselBorder")
    require("src/components/themes/lyra/LyraCarouselTheme.cpp", "void LyraCarouselTheme::drawCarouselBorder")
    require("src/components/themes/lyra/LyraCarouselTheme.cpp", "const std::string legacyThumbPath")
    require("src/components/themes/lyra/LyraCarouselTheme.cpp", "const std::string centerThumbPath")
    require("src/components/themes/lyra/LyraCarouselTheme.cpp", "LyraCarouselMetrics::values.homeCoverHeight")
    require("src/components/themes/lyra/LyraCarouselTheme.cpp", "kCenterCoverW, kCenterCoverH")
    carousel_theme = read("src/components/themes/lyra/LyraCarouselTheme.cpp")
    draw_cover_fn = carousel_theme[
        carousel_theme.index("auto drawCover = "):
        carousel_theme.index("if (!coverRendered)")
    ]
    if "LyraMetrics::values.homeCoverHeight" in draw_cover_fn:
        raise AssertionError("Lyra Carousel must not render the low-resolution Lyra thumb_226.bmp fallback")
    require("src/activities/home/HomeActivity.h", "kCarouselFrameCount")
    require("src/activities/home/HomeActivity.h", "preRenderCarouselFrames")
    require("src/activities/home/HomeActivity.cpp", "void HomeActivity::preRenderCarouselFrames")
    require("src/activities/home/HomeActivity.cpp", "void HomeActivity::renderCarouselFrame")
    require("src/activities/home/HomeActivity.cpp", "void HomeActivity::updateSlidingWindowCache")
    require("src/activities/home/HomeActivity.cpp",
            "if (wasFirstRenderDone && carouselTheme && recentsLoaded && !carouselFramesReady &&")

    home_activity = read("src/activities/home/HomeActivity.cpp")
    on_enter = home_activity[home_activity.index("void HomeActivity::onEnter()"):home_activity.index("void HomeActivity::onExit()")]
    if "preRenderCarouselFrames()" in on_enter:
        raise AssertionError("HomeActivity::onEnter should not prerender carousel frames before first display")

    render_prefix = home_activity[home_activity.index("void HomeActivity::render(RenderLock&&)"):home_activity.index("auto homeEntries = getHomeShortcutEntries")]
    if "preRenderCarouselFrames()" in render_prefix:
        raise AssertionError("HomeActivity::render should not prerender carousel frames before drawing the first screen")

    load_covers = home_activity[home_activity.index("void HomeActivity::loadRecentCovers"):home_activity.index("void HomeActivity::onEnter()")]
    if "LyraCarouselTheme::kSideCoverW" in load_covers:
        raise AssertionError("HomeActivity::loadRecentCovers should not generate side carousel thumbnails during startup")
    require("src/activities/home/HomeActivity.cpp", "if (isLyraCarouselTheme() && progress != lastCarouselBookIndex)")
    require("src/activities/home/HomeActivity.cpp", "void HomeActivity::scheduleCarouselCoverLoadIfNeeded")
    require("src/activities/home/HomeActivity.cpp", "epub.load(isLyraCarouselTheme(), true)")
    require("src/activities/home/HomeActivity.cpp", "lyra-carousel-frame-v6-carousel-thumb-only")
    require("src/activities/home/HomeActivity.cpp", "getCarouselCenterThumbPath")
    require("src/activities/home/HomeActivity.cpp", "hasCarouselUsableThumb")
    require("src/activities/home/HomeActivity.cpp", "LyraCarouselTheme::kCenterCoverW")
    require("src/activities/home/HomeActivity.cpp", "LyraCarouselTheme::kCenterCoverH")
    require("src/activities/home/HomeActivity.cpp", "hashCarouselThumbState")
    require("src/activities/home/HomeActivity.cpp", "if (!success && !isLyraCarouselTheme())")
    require("src/activities/home/HomeActivity.h", "carouselCoverLoadAttemptPath")
    require("src/activities/home/HomeActivity.cpp", "carouselCoverLoadAttemptPath = book.path")
    require("src/activities/home/HomeActivity.cpp", "book.path != carouselCoverLoadAttemptPath")
    require("src/activities/home/HomeActivity.cpp", "carouselCoverLoadAttemptPath.clear()")
    legacy_thumb_fn = home_activity[
        home_activity.index("bool hasCarouselUsableThumb"):
        home_activity.index("void HomeActivity::loadHomeCarouselBooks")
    ]
    if "LyraMetrics::values.homeCoverHeight" in legacy_thumb_fn:
        raise AssertionError(
            "Carousel cover loading must not treat thumb_226.bmp as a usable Carousel cover"
        )
    load_covers_after_checks = home_activity[
        home_activity.index("void HomeActivity::loadRecentCovers"):
        home_activity.index("void HomeActivity::scheduleCarouselCoverLoadIfNeeded")
    ]
    if "epub.generateThumbBmp(coverHeight)" not in load_covers_after_checks:
        raise AssertionError("Non-carousel cover loading should still use the active theme height")
    require("src/activities/home/HomeActivity.cpp",
            "epub.generateThumbBmp(LyraCarouselTheme::kCenterCoverW")
    require("src/activities/home/HomeActivity.cpp",
            "xtc.generateThumbBmp(LyraCarouselTheme::kCenterCoverW")

    # preRenderCarouselFrames must render only the center frame (slot 0), not all
    # three, so the user sees their selected book immediately on first paint and
    # adjacent frames are filled lazily by updateSlidingWindowCache().
    pre_render_fn = home_activity[
        home_activity.index("void HomeActivity::preRenderCarouselFrames"):
        home_activity.index("void HomeActivity::updateSlidingWindowCache")
    ]
    if "for (int slot = 0; slot < kCarouselFrameCount" in pre_render_fn:
        raise AssertionError(
            "preRenderCarouselFrames must not loop over all carousel slots; "
            "render only slot 0 (center frame) and let updateSlidingWindowCache fill the rest"
        )
    require("src/activities/home/HomeActivity.cpp", "renderCarouselFrame(0, centerIdx);")

    # renderCarouselFrame must pass bookCount (not safeBookIndex) to drawRecentBookCover
    # so that pre-rendered frames are stored with inCarouselRow=false (thin outline).
    # drawCarouselBorder() then overlays the thick selection border at display time.
    require("src/activities/home/HomeActivity.cpp", "recentBooks, bookCount, localCoverRendered")

    # drawCarouselBorder must return early when not in carousel row; the thin outline
    # is already baked into the cached frame.
    require("src/components/themes/lyra/LyraCarouselTheme.cpp", "if (!inCarouselRow) return;")


if __name__ == "__main__":
    main()
