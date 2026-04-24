"""
Configuration interface for the Epub Metaguider interface plugin.
"""

__license__ = "GPL v3"
__copyright__ = "2025, Hugo Batista <intellireading at hugobatista.com>"
__docformat__ = "markdown en"

try:
    from qt.core import QWidget, QVBoxLayout, QRadioButton, QGroupBox, QLabel
except ImportError:
    from PyQt5.Qt import QWidget, QVBoxLayout, QRadioButton, QGroupBox, QLabel

from calibre_plugins.metaguideinterface.config import prefs


class ConfigWidget(QWidget):
    """Configuration widget for the Epub Metaguider interface plugin."""

    def __init__(self, plugin_action):
        QWidget.__init__(self)
        self.layout = QVBoxLayout()
        self.setLayout(self.layout)

        # Default action settings
        action_group = QGroupBox("Default Action")
        action_layout = QVBoxLayout()

        # Epub radio button
        self.epub_radio = QRadioButton("Metaguide epub", self)
        self.epub_radio.setChecked(prefs["default_action"] == "epub")
        action_layout.addWidget(self.epub_radio)

        # Kepub radio button
        self.kepub_radio = QRadioButton("Metaguide kepub", self)
        self.kepub_radio.setChecked(prefs["default_action"] == "kepub")
        action_layout.addWidget(self.kepub_radio)

        action_group.setLayout(action_layout)
        self.layout.addWidget(action_group)

        # Add info label
        info_label = QLabel("Note: This setting affects which action is performed when clicking the toolbar button.")
        self.layout.addWidget(info_label)

        self.layout.addStretch()

    def save_settings(self):
        """Save the current configuration."""
        prefs["default_action"] = "kepub" if self.kepub_radio.isChecked() else "epub"
