__license__ = "GPL v3"
__copyright__ = "2025, Hugo Batista <intellireading at hugobatista.com>"

# pylint: disable=import-error
# pylint: disable=undefined-variable
try:
    from qt.core import QToolButton, QMenu
except ImportError:
    from PyQt5.Qt import QToolButton, QMenu
from calibre.gui2 import question_dialog


# The class that all interface action plugins must inherit from
from calibre.gui2.actions import (
    InterfaceAction,
)

from calibre_plugins.metaguideinterface import (
    common,
    metaguiding,
    config,
    __about_cli__,
)


# Donate message for first-time users
MSG_WELCOME = (
    "This plugin metaguides the selected books on your Calibre library.\n\n"
    "Metaguiding improves focus and reading speed by highlighting "
    "the beginning of words (sometimes called bionic reading).\n\n"
    "If you use a Kobo device, you can now use the 'KoboTouch - Metaguide Driver' plugin "
    "to automatically convert books to metaguided format when sending them to your device.\n\n"
    "To use it, download it from the calibre plugin store and install it.\n\n"
    "💙 Thank you for using IntelliReading!\n"
    "If this plugin improves your reading experience, please consider supporting "
    "its development with a donation. Your contribution helps maintain and improve "
    "these tools for the reading community.\n\n"
    "🌐 For more information: https://go.hugobatista.com/intellireading \n\n"
    "Lib Version: " + __about_cli__.__version__
)


class InterfacePlugin(InterfaceAction):
    """Interface plugin for metaguiding epubs"""

    name = "EBook Metaguider Interface Plugin (intellireading)"

    # Create our top-level menu/toolbar action (text, icon_path, tooltip, keyboard shortcut)
    action_spec = (
        _("Metaguide"),  # type: ignore # noqa
        None,
        _("Generate a metaguided epub/kepub"),  # type: ignore # noqa
        "Shift+M",
    )
    action_type = "current"
    popup_type = QToolButton.MenuButtonPopup
    dont_add_to = frozenset([])  # type: ignore
    dont_remove_from = frozenset([])  # type: ignore

    def genesis(self):
        # This method is called once per plugin, do initial setup here

        # Set the icon for this interface action
        _icon = get_icons("images/icon.png", "Metaguide")  # type: ignore # noqa

        # The qaction is automatically created from the action_spec defined above
        self.qaction.setIcon(_icon)

        # Create the menu
        self.menu = self.qaction.menu()
        if self.menu is None:
            self.menu = QMenu()
            self.qaction.setMenu(self.menu)

        # Add default action based on configuration
        self.qaction.triggered.connect(
            self.metaguide_kepub_selection
            if config.prefs["default_action"] == "kepub"
            else self.metaguide_epub_selection
        )

        # Add epub action to menu
        epub_action = self.menu.addAction(_("Metaguide epub"))  # type: ignore # noqa
        epub_action.triggered.connect(self.metaguide_epub_selection)

        # Add kepub action to menu
        kepub_action = self.menu.addAction(_("Metaguide kepub"))  # type: ignore # noqa
        kepub_action.triggered.connect(self.metaguide_kepub_selection)

        # Add separator
        self.menu.addSeparator()

        # Add remove metaguiding action to menu
        remove_metaguiding_action = self.menu.addAction(_("Remove metaguiding"))  # type: ignore # noqa
        remove_metaguiding_action.triggered.connect(self.remove_metaguiding_epub_selection)

        # Add separator
        self.menu.addSeparator()
        # Add an about action to the menu
        about_action = self.menu.addAction(_("About"))  # type: ignore # noqa
        about_action.triggered.connect(self.show_about_dialog)

        # point the metaguiding logger to the common logger
        metaguiding._logger = common.log

    def _show_warning_dialog(self, remove_metaguiding: bool) -> bool:
        """Show a warning dialog to the user before proceeding with metaguiding operations.

        Args:
            remove_metaguiding: Whether we're removing metaguiding (True) or adding it (False)

        Returns:
            bool: True if the user wants to proceed, False if they want to cancel
        """
        if remove_metaguiding:
            return question_dialog(
                self.gui,
                "Remove metaguiding is an EXPERIMENTAL feature",
                "This feature is still in development and may not work as expected. "
                "It should only be used if you deleted your ORIGINAL_format file and you want to try "
                "to remove the metaguiding. Some original format may not be restored. "
                "Please make sure to backup your library before using this feature.\n\n"
                "Do you want to continue?",
                show_copy_button=False,
                default_yes=False,
            )
        else:
            return question_dialog(
                self.gui,
                "Metaguiding will modify your files",
                "This feature will modify your files by adding metaguiding. "
                "The original file will be saved as ORIGINAL_format (ex: ORIGINAL_EPUB), and the new file will overwrite the original file. "
                "Any previous ORIGINAL_format file will be overwritten and lost. "
                "Please make sure to backup your library before using this feature.\n\n"
                "Do you want to continue?",
                show_copy_button=False,
                default_yes=False,
            )

    def _process_single_book(self, current_database, book_id, format_to_find, remove_metaguiding):
        from calibre.gui2 import error_dialog

        """Process a single book for metaguiding operations.
        
        Args:
            current_database: The current calibre database
            book_id: The ID of the book to process
            format_to_find: The format to look for (epub/kepub)
            remove_metaguiding: Whether to remove metaguiding
            
        Returns:
            bool: True if successful, False if an error occurred
        """
        temp_file = current_database.format(book_id, format_to_find, as_path=True)
        book_title = current_database.field_for("title", book_id)
        common.log.debug("Converting book id: %d, format: %s" % (book_id, format_to_find))

        self.gui.status_bar.show_message(f"Processing '{book_title}'...", 500)
        try:

            if not remove_metaguiding and metaguiding.is_file_metaguided(temp_file):
                log_message = f"Book '{book_title}' is already metaguided, skipping... (Format: {format_to_find})"
                common.log.debug(log_message)
                self.gui.status_bar.show_message(log_message, 1000)
                return True

            metaguiding.metaguide_epub_file(temp_file, temp_file, remove_metaguiding=remove_metaguiding)
        except Exception as e:  # pylint: disable=broad-except
            log_message = f"Error processing book '{book_title}', format: {format_to_find}, error details: {e}"
            common.log.error(log_message)
            self.gui.status_bar.show_message(log_message, 5000)
            error_dialog(
                self.gui,
                f"Error processing book '{book_title}'. Please verify that the epub is valid.",
                "This error may be caused by a corrupted file or an unsupported format.\n\n"
                "Please check the file and try again.\n\n"
                "If the problem persists, please report it to the plugin author.\n\n"
                f"Error details: {str(e)}\n\n",
                show=True,
            )
            return False

        self.gui.status_bar.show_message(f"'Operation completed on book '{book_title}'", 3000)
        current_database.save_original_format(book_id, format_to_find)
        result = current_database.add_format(book_id, format_to_find, temp_file, replace=True, run_hooks=False)

        if not result:
            error_dialog(
                self.gui,
                "Operation failed",
                f"Processing book '{book_title}' failed. (no result)\n\n"
                "Please verify that the file is valid and try again.\n\n"
                "If the problem persists, please report it to the plugin author.\n\n",
                show=True,
            )
            return False

        return True

    def show_about_dialog(self):
        common.show_donate_message(
            title="EBook Metaguider (intellireading) - About", message=MSG_WELCOME, skip_dialog_name=None
        )

    def metaguide_selection_format(self, format_to_find: str, *, remove_metaguiding: bool = False):
        from calibre.gui2 import error_dialog

        common.show_donate_message(
            title="EBook Metaguider (intellireading) - Welcome!",
            message=MSG_WELCOME,
            skip_dialog_name="ebook_metaguider_donate",
        )

        # Show warning dialog
        if not self._show_warning_dialog(remove_metaguiding):
            return

        # Get currently selected books
        selected_rows = self.gui.library_view.selectionModel().selectedRows()
        if not selected_rows or len(selected_rows) == 0:
            return error_dialog(self.gui, "Cannot process books", "No books selected", show=True)

        # Map the rows to book ids
        selected_ids = list(map(self.gui.library_view.model().id, selected_rows))
        current_database = self.gui.current_db.new_api
        epubs_found_count = 0

        for book_id in selected_ids:
            if current_database.has_format(book_id, format_to_find):
                epubs_found_count += 1
                if not self._process_single_book(current_database, book_id, format_to_find, remove_metaguiding):
                    return

        # If we are here, it means that we have processed all the files
        # check if we have processed any files
        if epubs_found_count == 0:
            return error_dialog(
                self.gui,
                "Cannot process books",
                "No books with format %s found" % (format_to_find),
                show_copy_button=False,
                show=True,
            )

        self.gui.status_bar.show_message(
            f"Successfully processed {epubs_found_count} files from {len(selected_ids)} books", 3000
        )
        current_idx = self.gui.library_view.currentIndex()
        self.gui.library_view.model().current_changed(current_idx, current_idx)
        self.gui.library_view.model().refresh_ids(selected_ids)

    def metaguide_epub_selection(self):
        self.metaguide_selection_format("epub")

    def metaguide_kepub_selection(self):
        self.metaguide_selection_format("kepub")

    def remove_metaguiding_epub_selection(self):
        self.metaguide_selection_format("epub", remove_metaguiding=True)

    def apply_settings(self):
        """
        Called when the plugin's configuration has been changed.
        Update the toolbar button's action.
        """
        self.qaction.triggered.disconnect()
        self.qaction.triggered.connect(
            self.metaguide_kepub_selection
            if config.prefs["default_action"] == "kepub"
            else self.metaguide_epub_selection
        )
