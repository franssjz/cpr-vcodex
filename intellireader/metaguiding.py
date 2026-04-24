import logging
from io import BytesIO
import os
import sys
import traceback
import zipfile
from typing import Generator
import math
import regex as re

# relative import is used to ensure that when this module gets copied to another location
# it still works
from .__about_cli__ import __version__ as cli_version  # noqa: TID252


_logger = logging.getLogger(__name__)
_METAGUIDED_FLAG_FILENAME = "intellireading.metaguide"
_EPUB_EXTENSIONS = [".EPUB", ".KEPUB"]
_XHTML_EXTENSIONS = [".XHTML", ".HTML", ".HTM"]
_TOC_FILENAMES = ["nav.xhtml", "nav.html", "toc.xhtml", "toc.html"]


def _generate_flag_file_content() -> bytes:
    """Generate the content for the metaguiding flag file.
    This includes version, process name, and call graph information.

    Returns:
        bytes: The encoded content of the flag file.
    """
    try:
        # Get process name - fallback to 'unknown' if sys.argv is not available
        try:
            process_name = os.path.basename(sys.argv[0])
        except (AttributeError, IndexError):
            process_name = "unknown"
            _logger.warning("Could not determine process name, using 'unknown'")

        # Get call stack information - handle potential traceback errors
        try:
            call_frames = traceback.extract_stack()
            # Skip the last frame (current function) and metaguide_epub_stream frame
            relevant_frames = call_frames[:-2]
            call_graph_path = " -> ".join(
                f"{os.path.basename(frame.filename)}:{frame.name}" for frame in relevant_frames
            )
        except Exception as e:
            call_graph_path = "unknown"
            _logger.warning(f"Could not generate call graph: {e}")

        # Build and return the flag content
        flag_lines = [f"version: {cli_version}", f"process: {process_name}", f"call_graph: {call_graph_path}"]
        return "\n".join(flag_lines).encode()
    except Exception as e:
        # If anything goes wrong, return a minimal flag file rather than failing
        _logger.error(f"Error generating flag file content: {e}")
        return f"version: {cli_version}\nprocess: unknown\ncall_graph: error".encode()


class RegExBoldMetaguider:
    _body_regex = re.compile(r"<body[^>]*>(.*)</body>", re.DOTALL)
    _text_block_regex = re.compile(r"(?<!<b[^>]*)>[^\S]*[^\s<][^<]*[^\S\n]*<")
    _bolded_text_block_regex = re.compile(r"<b>\b\w{1}\b</b>|<b>\b\w+\b</b>(?:\b\w+\b)")
    _word_pattern_regex = re.compile(r"\b\w+\b", re.UNICODE)
    _entity_ref_regex = re.compile(r"(&[#a-zA-Z][a-zA-Z0-9]*;)")
    _bolded_word_regex = re.compile(r"<b>(.*?)</b>")

    def __init__(self, fallback_encoding: str = "utf-8") -> None:
        self._fallback_encoding = fallback_encoding

    def _bold_word(self, word: str) -> str:
        # this is the function that is called for each word

        # if the word is an empty string, whitespace or new line, return it
        if not word.strip():
            return word

        word_length = len(word)
        midpoint = 1 if word_length in (1, 3) else math.ceil(word_length / 2)
        return f"<b>{word[:midpoint]}</b>{word[midpoint:]}"  # Bold the first half of the word

    def _unbold_word(self, word: str) -> str:
        # if the word is an empty string, whitespace or new line, return it
        if not word.strip():
            return word

        # short-circuit if the word is not bolded
        if "<b>" not in word:
            return word

        # remove the <b></b> tags
        return self._bolded_word_regex.sub(r"\1", word)

    def _bold_node_text_part(self, part: str) -> str:
        # is this part an entity reference?
        if self._entity_ref_regex.match(part):
            return part
        return self._word_pattern_regex.sub(lambda m: self._bold_word(m.group()), part)

    def _unbold_node_text_part(self, part: str) -> str:
        # skip if it's an entity reference
        if self._entity_ref_regex.match(part):
            _logger.debug(f"Skipping entity reference: {part}")
            return part
        # remove bold tags on all words found
        result = self._unbold_word(part)
        return result

    def _bold_text_node(self, node: str) -> str:
        # this is the function that is called for each text node
        node_text = node[1:-1]
        _logger.debug(f"Bold text node: {node_text}")

        # split the node_text into parts based on the entity references
        node_text_parts = self._entity_ref_regex.split(node_text)

        new_node_text = "".join(map(self._bold_node_text_part, node_text_parts))

        if node_text != new_node_text:
            return ">" + new_node_text + "<"
        return node

    def _bold_document(self, html: str, *, remove_metaguiding: bool = False) -> str:
        # get the body. If there is no body, return the original html
        match = self._body_regex.search(html)
        if not match:
            return html

        body = match.group(1)
        if not remove_metaguiding:
            # find all text nodes in the body and trigger the bolding of the words
            body = self._text_block_regex.sub(
                lambda m: self._bold_text_node(m.group()),
                body,
            )
        else:
            body = self._bolded_text_block_regex.sub(lambda m: self._unbold_node_text_part(m.group()), body)

        _logger.debug(f"Bolded body: {body}")

        html = html.replace(match.group(1), body)
        return html

    def _get_encoding_using_lxml(self, xhtml_document: bytes) -> str | None:
        from lxml import etree

        parser = etree.XMLParser(resolve_entities=False)
        doc = etree.fromstring(xhtml_document, parser=parser).getroottree()
        docinfo = doc.docinfo
        return docinfo.encoding

    def _get_encoding_using_bom(self, xhtml_document: bytes) -> str | None:
        if xhtml_document.startswith(b"\xef\xbb\xbf"):
            return "utf-8"
        elif xhtml_document.startswith(b"\xff\xfe"):
            return "utf-16-le"
        elif xhtml_document.startswith(b"\xfe\xff"):
            return "utf-16-be"
        elif xhtml_document.startswith(b"\x00\x00\xfe\xff"):
            return "utf-32-be"
        elif xhtml_document.startswith(b"\xff\xfe\x00\x00"):
            return "utf-32-le"
        else:
            return None

    def _get_encoding_using_xml_header(self, xhtml_document: bytes) -> str | None:
        # if the document does not start with an XML header, return None. This is not a xml document
        if not xhtml_document.startswith(b"<?xml "):
            return None

        xml_header_end = xhtml_document.find(b"?>") + 1
        if xml_header_end == 0:
            msg = "Invalid XHTML document. Could not find closing XML element."
            raise ValueError(msg)

        header = xhtml_document[:xml_header_end].decode("utf-8")
        match = re.search(r'encoding=(["\'])([a-zA-Z][a-zA-Z0-9-]{0,38}[a-zA-Z0-9])\1', header)
        if match:
            return match.group(2)
        else:
            # although the XML header is present, it does not contain an encoding
            # this is not a valid XHTML document, but we can still try to detect the encoding on a later stage
            # and we will not raise an exception
            return None

    def _get_encoding(self, xhtml_document: bytes) -> str:
        encoding = self._get_encoding_using_xml_header(xhtml_document)

        if not encoding:
            _logger.debug(
                "Could not detect the encoding of the XHTML document using the XML header. "
                "Trying to detect the encoding using the BOM."
            )
            encoding = self._get_encoding_using_bom(xhtml_document)

        if not encoding:
            _logger.debug(
                "Could not detect the encoding of the XHTML document. Trying to detect the encoding using lxml."
            )
            encoding = self._get_encoding_using_lxml(xhtml_document)

        return encoding or self._fallback_encoding

    def metaguide_xhtml_document(self, xhtml_document: bytes, *, remove_metaguiding: bool = False) -> bytes:
        # if none of the methods to detect the encoding work, use utf-8
        encoding = self._get_encoding(xhtml_document) or "utf-8"

        html = xhtml_document.decode(encoding)
        bolded_html = self._bold_document(html, remove_metaguiding=remove_metaguiding)
        return bolded_html.encode(encoding)


class _EpubItemFile:

    def __init__(self, filename: str | None = None, content: bytes = b"") -> None:
        self.filename = filename
        self.content = content
        _extension = (self.filename and os.path.splitext(self.filename)[-1].upper()) or None

        # some epub have files with html extension but they are xml files
        self.is_xhtml_document = _extension in _XHTML_EXTENSIONS
        # check whether the file is a table of contents or navigation file
        # by checking the filename against a list of known TOC filenames
        # this is a heuristic and may not be 100% accurate.
        # Separate the directory check from the filename check
        self.is_toc_document = False
        if self.filename:
            # Normalize the filename to lowercase for comparison and remove the directory part
            filename_base = os.path.basename(self.filename.lower())
            self.is_toc_document = filename_base in _TOC_FILENAMES
        self.metaguided = False  # flag to indicate if the file has been metaguided. Useful for multi-threading

    def __str__(self) -> str:
        return f"{self.filename} ({len(self.content)} bytes)"

    def metaguide(self, metaguider: RegExBoldMetaguider, *, remove_metaguiding: bool = False):
        if not remove_metaguiding and self.metaguided:
            _logger.warning(f"File {self.filename} already metaguided, skipping")
        elif self.is_toc_document:
            _logger.debug(f"Skipping nav/toc file {self.filename}")
        elif self.is_xhtml_document:
            _logger.debug(f"Metaguiding file {self.filename}")
            self.content = metaguider.metaguide_xhtml_document(self.content, remove_metaguiding=remove_metaguiding)
            self.metaguided = True
            _logger.debug(f"Metaguided file {self.filename}")
        else:
            _logger.debug(f"Skipping file {self.filename}")


_metaguider = RegExBoldMetaguider()


def _get_epub_item_files_from_zip(input_zip: zipfile.ZipFile) -> list:
    def read_compressed_file(input_zip: zipfile.ZipFile, filename: str) -> _EpubItemFile:
        return _EpubItemFile(filename, input_zip.read(filename))

    epub_item_files = [read_compressed_file(input_zip, f.filename) for f in input_zip.infolist()]
    _logger.debug(f"Read {len(epub_item_files)} files from input file")
    return epub_item_files


def _process_epub_item_files(
    epub_item_files: list[_EpubItemFile], *, remove_metaguiding: bool = False
) -> Generator[_EpubItemFile, None, None]:
    for epub_item_file in epub_item_files:
        _logger.debug(f"Processing file '{epub_item_file.filename}' remove_metaguiding={remove_metaguiding}")
        epub_item_file.metaguide(_metaguider, remove_metaguiding=remove_metaguiding)
        yield epub_item_file


def _write_item_files_to_zip(epub_item_files, output_zip):
    def write_compressed_file(output_zip: zipfile.ZipFile, epub_item_file: _EpubItemFile):
        if epub_item_file.filename is None:
            msg = "EpubItemFile.filename is None"
            raise ValueError(msg)

        _logger.debug(f"Writing file {epub_item_file.filename} to output zip {output_zip.filename}")
        with output_zip.open(epub_item_file.filename, mode="w") as compressed_output_file:
            compressed_output_file.write(epub_item_file.content)

    for _epub_item_file in epub_item_files:
        write_compressed_file(output_zip, _epub_item_file)


def _ensure_file_exists(input_file: str):
    if not os.path.isfile(input_file):
        exception_message = f"Input file '{input_file}' does not exist"
        _logger.error(exception_message)
        raise ValueError(exception_message)


def _ensure_allowed_extension(input_file: str, extensions: list[str]):
    file_extension = os.path.splitext(input_file)[-1].upper()
    if file_extension not in extensions:
        exception_message = f"Input file '{input_file}' extension is not in {extensions}"
        _logger.error(exception_message)
        raise ValueError(exception_message)


def _check_flag_file(
    epub_item_files: list[_EpubItemFile], *, remove_metaguiding: bool = False
) -> tuple[bool, _EpubItemFile | None]:
    """Check if an epub file is already metaguided by looking for the flag file.

    Args:
        epub_item_files: List of files in the epub
        remove_metaguiding: Whether we're removing metaguiding

    Returns:
        Tuple of (is_already_metaguided, flag_file)
    """
    flag_file = next((f for f in epub_item_files if f.filename == _METAGUIDED_FLAG_FILENAME), None)
    is_already_metaguided = not remove_metaguiding and flag_file is not None

    if is_already_metaguided:
        try:
            _logger.debug("Epub already metaguided, flag file content:")
            if flag_file:
                _logger.debug(flag_file.content.decode("utf-8"))
            else:
                _logger.debug("Flag file found but content could not be read")
        except Exception as e:
            _logger.debug(f"Could not decode flag file content: {e}")

    return is_already_metaguided, flag_file


def metaguide_epub_file(input_file: str, output_file: str, *, remove_metaguiding: bool = False):
    """Metaguide an epub file
    input_file: str
        The input epub file
    output_file: str
        The output epub file
    remove_metaguiding: bool
        If True, removes metaguiding from the epub file
    """

    _logger.debug(f"Processing file '{input_file}' to output '{output_file}'")
    _ensure_file_exists(input_file)
    _ensure_allowed_extension(input_file, _EPUB_EXTENSIONS)

    with open(input_file, "rb") as input_reader:
        input_file_stream = BytesIO(input_reader.read())
        output_file_stream = metaguide_epub_stream(input_file_stream, remove_metaguiding=remove_metaguiding)
        with open(output_file, "wb") as output_writer:
            output_writer.write(output_file_stream.read())


def metaguide_epub_stream(input_stream: BytesIO, *, remove_metaguiding: bool = False) -> BytesIO:
    """Metaguide an epub input stream
    input_file_stream: BytesIO
        The input epub file stream
    remove_metaguiding: bool
        If True, removes metaguiding from the epub file
    return: BytesIO
        The metaguided epub file stream
    """
    output_stream = BytesIO()

    if remove_metaguiding:
        _logger.debug("Removing metaguiding from epub")
    else:
        _logger.debug("Metaguiding epub")

    _logger.debug("Getting item files")
    with zipfile.ZipFile(input_stream, "r", compression=zipfile.ZIP_DEFLATED, allowZip64=True) as input_zip:
        with zipfile.ZipFile(output_stream, "w", compression=zipfile.ZIP_DEFLATED, allowZip64=True) as output_zip:
            _logger.debug("Processing zip: Getting item files")
            epub_item_files = _get_epub_item_files_from_zip(input_zip)

            # Check if the file is already metaguided
            is_already_metaguided, _ = _check_flag_file(epub_item_files, remove_metaguiding=remove_metaguiding)

            if is_already_metaguided:
                _logger.debug("Copying files while preserving structure...")
                # Even for already metaguided files, we need to properly process through zip mechanisms
                for item in input_zip.filelist:
                    with input_zip.open(item.filename) as source, output_zip.open(item.filename, "w") as target:
                        _logger.debug(f"Copying file {item.filename} with original compression")
                        target.write(source.read())
            else:
                processed_item_files = list(
                    _process_epub_item_files(epub_item_files, remove_metaguiding=remove_metaguiding)
                )

                if remove_metaguiding:
                    # remove the metaguided flag file
                    filtered_files = filter(lambda f: f.filename != _METAGUIDED_FLAG_FILENAME, processed_item_files)
                    processed_item_files = list(filtered_files)
                else:
                    _logger.debug("Processing zip: Adding metaguided flag file")
                    flag_content = _generate_flag_file_content()
                    processed_item_files.append(_EpubItemFile(_METAGUIDED_FLAG_FILENAME, flag_content))

                _logger.debug("Processing zip: Writing output zip")
                _write_item_files_to_zip(processed_item_files, output_zip)

    output_stream.seek(0)
    return output_stream


def metaguide_xhtml_file(input_file: str, output_file: str, *, remove_metaguiding: bool = False):
    """Metaguide an xhtml file
    input_file: str
        The input xhtml file
    output_file: str
        The output xhtml file
    remove_metaguiding: bool
        If True, removes metaguiding from the xhtml file
    """
    _logger.debug(f"Processing file '{input_file}' to output '{output_file}'")
    _ensure_file_exists(input_file)
    _ensure_allowed_extension(input_file, _XHTML_EXTENSIONS)

    with open(input_file, "rb") as input_reader:
        input_file_stream = BytesIO(input_reader.read())
        output_file_stream = metaguide_xhtml_stream(input_file_stream, remove_metaguiding=remove_metaguiding)
    output_file_stream.seek(0)
    with open(output_file, "wb") as output_writer:
        output_writer.write(output_file_stream.read())


def metaguide_xhtml_stream(input_file_stream: BytesIO, *, remove_metaguiding: bool = False) -> BytesIO:
    """Metaguide an xhtml input stream
    input_file_stream: BytesIO
        The input xhtml file stream
    remove_metaguiding: bool
        If True, removes metaguiding from the xhtml file
    return: BytesIO
        The metaguided xhtml file stream
    """
    output_file_stream = BytesIO()
    output_file_stream.write(
        _metaguider.metaguide_xhtml_document(input_file_stream.read(), remove_metaguiding=remove_metaguiding)
    )
    output_file_stream.seek(0)
    return output_file_stream


def metaguide_dir(input_dir: str, output_dir: str, *, remove_metaguiding: bool = False):
    """Metaguides all epubs and xhtml found in a directory (recursively)
    input_dir: str
        The input epub/xhtml directory
    output_dir: str
        The output epub/xhtml directory
    remove_metaguiding: bool
        If True, removes metaguiding from the files
    """

    # get a list of all the files in the directory, and the child directories if recursive
    # verify if the file is a file and if it has the correct extension
    def get_files(directory, recursive):
        for filename in os.listdir(directory):
            input_filename = os.path.join(directory, filename)

            extension = os.path.splitext(input_filename)[-1].upper()
            if os.path.isfile(input_filename) and (extension in _EPUB_EXTENSIONS or extension in _XHTML_EXTENSIONS):
                yield input_filename
            elif os.path.isdir(input_filename) and recursive:
                yield from get_files(input_filename, recursive)

    _logger.info(f"Processing files in {input_dir} to {output_dir} (recursively)")

    files_processed = 0
    files_skipped = 0
    files_with_errors = 0

    # check if the output directory exists and if not create it
    if not os.path.exists(output_dir):
        _logger.info(f"Creating {output_dir}")
        os.makedirs(output_dir)

    for input_filename in get_files(input_dir, True):
        output_filename = os.path.join(output_dir, os.path.basename(input_filename))
        _logger.debug(f"Processing {input_filename} to {output_filename}")

        # verify if the output file already exists
        if os.path.isfile(output_filename):
            _logger.warning(f"Skipping {input_filename} because {output_filename} already exists")
            files_skipped += 1
            continue

        try:
            with open(input_filename, "rb") as input_reader:
                input_file_stream = BytesIO(input_reader.read())
                if os.path.splitext(input_filename)[-1].upper() in _EPUB_EXTENSIONS:
                    output_file_stream = metaguide_epub_stream(input_file_stream, remove_metaguiding=remove_metaguiding)
                else:
                    output_file_stream = metaguide_xhtml_stream(
                        input_file_stream, remove_metaguiding=remove_metaguiding
                    )
                with open(output_filename, "wb") as output_writer:
                    output_writer.write(output_file_stream.read())
            files_processed += 1
        except Exception as e:  # pylint: disable=broad-except
            # pylint: disable=logging-fstring-interpolation
            files_with_errors += 1
            _logger.exception(f"Error processing {input_filename}", e)
            continue


def is_file_metaguided(filepath: str) -> bool:
    """Check if a file has already been metaguided.

    Args:
        filepath: Path to the epub file to check

    Returns:
        bool: True if the file has already been metaguided, False otherwise

    Raises:
        ValueError: If the file doesn't exist or has an invalid extension
    """
    _ensure_file_exists(filepath)
    _ensure_allowed_extension(filepath, _EPUB_EXTENSIONS)

    with open(filepath, "rb") as input_reader:
        input_file_stream = BytesIO(input_reader.read())
        with zipfile.ZipFile(input_file_stream, "r", compression=zipfile.ZIP_DEFLATED, allowZip64=True) as input_zip:
            epub_item_files = _get_epub_item_files_from_zip(input_zip)
            is_metaguided, _ = _check_flag_file(epub_item_files)
            return is_metaguided
