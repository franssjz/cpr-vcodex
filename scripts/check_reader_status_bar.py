from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def main() -> None:
    epub_reader = read("src/activities/reader/EpubReaderActivity.cpp")
    gray_block = epub_reader[
        epub_reader.index("if (enableTextAA || enableImageGrayscaleOnly)"):
        epub_reader.index("renderer.displayGrayBuffer();")
    ]
    if gray_block.count("renderStatusBar();") < 2:
        raise AssertionError(
            "EPUB grayscale rendering must include the status bar in both gray buffers"
        )


if __name__ == "__main__":
    main()
