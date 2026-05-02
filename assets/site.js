(() => {
  const LANGUAGE_KEY = "cprToolsLanguage";
  const THEME_KEY = "cprToolsTheme";
  const LEGACY_LANGUAGE_KEY = "cprStatsLanguage";
  const LEGACY_THEME_KEY = "cprStatsTheme";
  const languages = [
    ["be", "Беларуская"],
    ["ca", "Català"],
    ["cs", "Čeština"],
    ["da", "Dansk"],
    ["nl", "Nederlands"],
    ["en", "English"],
    ["fi", "Suomi"],
    ["fr", "Français"],
    ["de", "Deutsch"],
    ["hu", "Magyar"],
    ["it", "Italiano"],
    ["kk", "Қазақша"],
    ["lt", "Lietuvių"],
    ["pl", "Polski"],
    ["pt", "Português (Brasil)"],
    ["ro", "Română"],
    ["ru", "Русский"],
    ["si", "Slovenščina"],
    ["es", "Español"],
    ["sv", "Svenska"],
    ["tr", "Türkçe"],
    ["uk", "Українська"],
    ["vi", "Tiếng Việt"]
  ];
  const locales = {
    be: "be",
    ca: "ca",
    cs: "cs",
    da: "da",
    nl: "nl",
    en: "en",
    fi: "fi",
    fr: "fr",
    de: "de",
    hu: "hu",
    it: "it",
    kk: "kk",
    lt: "lt",
    pl: "pl",
    pt: "pt-BR",
    ro: "ro",
    ru: "ru",
    si: "sl",
    es: "es",
    sv: "sv",
    tr: "tr",
    uk: "uk",
    vi: "vi"
  };
  const messages = {
    en: {
      navHome: "Home",
      navStats: "Reading Stats Editor",
      navFlash: "Auto Flash",
      darkMode: "Dark mode",
      lightMode: "Light mode",
      homeTitle: "CPR-vCodex Tools",
      homeSubtitle: "Small browser tools for CPR-vCodex. Everything runs locally in your browser.",
      latestFirmware: "Latest firmware: 1.2.0.37-cpr-vcodex",
      homeFlash: "Auto Flash",
      homeStats: "Reading Stats Editor",
      flashEyebrow: "Browser-based installer",
      flashTitle: "Flash CPR-vCodex",
      flashSubtitle: "Install the latest CPR-vCodex firmware on your Xteink X4 from Chrome or Edge using Web Serial. It writes the inactive app partition and then switches boot to it.",
      beforeFlash: "Before you flash",
      backupTitle: "Back up first",
      backupText: "This OTA-style flash preserves bootloader, partition table, SD card and NVS settings, but backing up your SD card is still sensible.",
      cableTitle: "Use a data USB-C cable",
      cableText: "Connect the Xteink X4 to your computer. If no serial port appears, wake and unlock the device, then reconnect it.",
      browserTitle: "Use Chrome or Edge on desktop",
      browserText: "Firefox and Safari do not support Web Serial. The browser will ask you to choose the ESP32-C3 USB serial port.",
      unplugTitle: "Do not unplug while flashing",
      unplugText: "The page validates the partition table, writes the inactive OTA app slot, updates <code>otadata</code>, and resets the device when it finishes.",
      latestPackage: "Latest firmware package",
      flashWarning: "Use at your own risk. Flashing custom firmware can fail if the cable disconnects or the device loses power.",
      unsupported: "Web Serial is not available in this browser. Use Chrome or Edge on a desktop computer, or use the manual download below.",
      flashButton: "Flash CPR-vCodex Firmware",
      flashing: "Flashing...",
      readyStatus: "Ready. Clicking Flash will open the browser serial device chooser.",
      choosePort: "Choose the ESP32-C3 serial port in the browser prompt.",
      openingConnection: "Opening bootloader connection...",
      partitionOk: "Default CrossPoint/Xteink OTA partition table found.",
      fetchingFirmware: "Fetching firmware package...",
      flashComplete: "Flash complete. The device should reboot into CPR-vCodex.",
      flashStopped: "Flash stopped. Check the message above before trying again.",
      downloadBin: "Download .bin",
      communityFlasher: "Community flasher",
      flashNote: "The automatic flasher uses the firmware copy published with this page. The download button uses the GitHub release asset.",
      manualTitle: "Manual PlatformIO flash",
      manualText: "For development builds, clone the repo, set the upload port, then use PlatformIO.",
      stepPending: "Pending",
      stepRunning: "Running",
      stepDone: "Done",
      stepFailed: "Failed",
      stepConnect: "Connect to device",
      stepPartition: "Validate partition table",
      stepDownload: "Download firmware",
      stepReadOtadata: "Read otadata partition",
      stepFlashApp: "Flash app partition",
      stepFlashOtadata: "Flash otadata partition",
      stepReset: "Reset device"
    },
    es: {
      navHome: "Inicio",
      navStats: "Editor de estadísticas",
      navFlash: "Auto Flash",
      darkMode: "Modo oscuro",
      lightMode: "Modo claro",
      homeTitle: "Herramientas CPR-vCodex",
      homeSubtitle: "Pequeñas herramientas de navegador para CPR-vCodex. Todo se ejecuta localmente en tu navegador.",
      latestFirmware: "Último firmware: 1.2.0.37-cpr-vcodex",
      homeFlash: "Auto Flash",
      homeStats: "Editor de estadísticas",
      flashEyebrow: "Instalador desde navegador",
      flashTitle: "Flashear CPR-vCodex",
      flashSubtitle: "Instala el último firmware CPR-vCodex en tu Xteink X4 desde Chrome o Edge usando Web Serial. Escribe la partición de app inactiva y luego arranca desde ella.",
      beforeFlash: "Antes de flashear",
      backupTitle: "Haz copia primero",
      backupText: "Este flasheo tipo OTA conserva bootloader, tabla de particiones, tarjeta SD y ajustes NVS, pero hacer copia de la SD sigue siendo buena idea.",
      cableTitle: "Usa un cable USB-C de datos",
      cableText: "Conecta el Xteink X4 al ordenador. Si no aparece el puerto serie, despierta y desbloquea el dispositivo y vuelve a conectarlo.",
      browserTitle: "Usa Chrome o Edge en escritorio",
      browserText: "Firefox y Safari no soportan Web Serial. El navegador te pedirá elegir el puerto USB serie ESP32-C3.",
      unplugTitle: "No desconectes durante el flasheo",
      unplugText: "La página valida la tabla de particiones, escribe el slot OTA inactivo, actualiza <code>otadata</code> y reinicia el dispositivo al terminar.",
      latestPackage: "Último paquete de firmware",
      flashWarning: "Úsalo bajo tu responsabilidad. Flashear firmware personalizado puede fallar si se desconecta el cable o el dispositivo pierde energía.",
      unsupported: "Web Serial no está disponible en este navegador. Usa Chrome o Edge en un ordenador de escritorio, o descarga el binario manualmente.",
      flashButton: "Flashear firmware CPR-vCodex",
      flashing: "Flasheando...",
      readyStatus: "Listo. Al pulsar Flash se abrirá el selector de dispositivo serie del navegador.",
      choosePort: "Elige el puerto serie ESP32-C3 en el aviso del navegador.",
      openingConnection: "Abriendo conexión con el bootloader...",
      partitionOk: "Tabla de particiones OTA CrossPoint/Xteink por defecto detectada.",
      fetchingFirmware: "Descargando paquete de firmware...",
      flashComplete: "Flasheo completado. El dispositivo debería reiniciar en CPR-vCodex.",
      flashStopped: "Flasheo detenido. Revisa el mensaje anterior antes de intentarlo de nuevo.",
      downloadBin: "Descargar .bin",
      communityFlasher: "Flasher comunitario",
      flashNote: "El flasher automático usa la copia de firmware publicada con esta página. El botón de descarga usa el asset de la release de GitHub.",
      manualTitle: "Flasheo manual con PlatformIO",
      manualText: "Para builds de desarrollo, clona el repo, configura el puerto de subida y usa PlatformIO.",
      stepPending: "Pendiente",
      stepRunning: "En curso",
      stepDone: "Hecho",
      stepFailed: "Error",
      stepConnect: "Conectar al dispositivo",
      stepPartition: "Validar tabla de particiones",
      stepDownload: "Descargar firmware",
      stepReadOtadata: "Leer partición otadata",
      stepFlashApp: "Flashear partición app",
      stepFlashOtadata: "Flashear partición otadata",
      stepReset: "Reiniciar dispositivo"
    }
  };

  function hasLanguage(language) {
    return languages.some(([code]) => code === language);
  }

  function getLanguage() {
    const saved = localStorage.getItem(LANGUAGE_KEY) || localStorage.getItem(LEGACY_LANGUAGE_KEY);
    if (saved && hasLanguage(saved)) return saved;
    const browser = (navigator.language || "en").toLowerCase();
    const shortCode = browser.split("-")[0];
    return hasLanguage(browser) ? browser : hasLanguage(shortCode) ? shortCode : "en";
  }

  function getTheme() {
    const saved = localStorage.getItem(THEME_KEY) || localStorage.getItem(LEGACY_THEME_KEY);
    if (saved === "light" || saved === "dark") return saved;
    return window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
  }

  function t(key, language = getLanguage()) {
    return (messages[language] && messages[language][key]) || messages.en[key] || key;
  }

  function populateLanguageSelects(language = getLanguage()) {
    document.querySelectorAll("[data-role='language-select']").forEach(select => {
      select.innerHTML = languages
        .map(([code, label]) => `<option value="${code}">${label}</option>`)
        .join("");
      select.value = hasLanguage(language) ? language : "en";
    });
  }

  function applyTheme(theme = getTheme()) {
    const normalized = theme === "dark" ? "dark" : "light";
    document.documentElement.dataset.theme = normalized;
    if (document.body) document.body.dataset.theme = normalized;
    document.querySelectorAll("[data-role='theme-toggle']").forEach(button => {
      const label = t(normalized === "dark" ? "lightMode" : "darkMode");
      button.setAttribute("aria-label", label);
      button.setAttribute("title", label);
      const use = button.querySelector("use");
      if (use) {
        use.setAttribute("href", normalized === "dark" ? "#icon-sun" : "#icon-moon");
        return;
      }
      const iconHolder = button.querySelector("[data-role='theme-icon']") || button;
      iconHolder.innerHTML = normalized === "dark" ? sunIcon() : moonIcon();
    });
  }

  function applyI18n(language = getLanguage()) {
    const normalized = hasLanguage(language) ? language : "en";
    document.documentElement.lang = locales[normalized] || normalized;
    populateLanguageSelects(normalized);
    document.querySelectorAll("[data-site-i18n]").forEach(node => {
      node.textContent = t(node.dataset.siteI18n, normalized);
    });
    document.querySelectorAll("[data-site-i18n-html]").forEach(node => {
      node.innerHTML = t(node.dataset.siteI18nHtml, normalized);
    });
    document.querySelectorAll("[data-site-i18n-placeholder]").forEach(node => {
      node.placeholder = t(node.dataset.siteI18nPlaceholder, normalized);
    });
    applyTheme(getTheme());
  }

  function setLanguage(language, {emit = true} = {}) {
    const normalized = hasLanguage(language) ? language : "en";
    localStorage.setItem(LANGUAGE_KEY, normalized);
    localStorage.setItem(LEGACY_LANGUAGE_KEY, normalized);
    applyI18n(normalized);
    if (emit) {
      window.dispatchEvent(new CustomEvent("cpr-tools-language-change", {detail: {language: normalized}}));
    }
  }

  function setTheme(theme, {emit = true} = {}) {
    const normalized = theme === "dark" ? "dark" : "light";
    localStorage.setItem(THEME_KEY, normalized);
    localStorage.setItem(LEGACY_THEME_KEY, normalized);
    applyTheme(normalized);
    if (emit) {
      window.dispatchEvent(new CustomEvent("cpr-tools-theme-change", {detail: {theme: normalized}}));
    }
  }

  function wireControls() {
    document.querySelectorAll("[data-role='theme-toggle']").forEach(button => {
      if (button.dataset.cprToolsWired) return;
      button.dataset.cprToolsWired = "true";
      button.addEventListener("click", () => {
        setTheme(getTheme() === "dark" ? "light" : "dark");
      });
    });
    document.querySelectorAll("[data-role='language-select']").forEach(select => {
      if (select.dataset.cprToolsWired) return;
      select.dataset.cprToolsWired = "true";
      select.addEventListener("change", () => setLanguage(select.value));
    });
  }

  function init({wireControls: shouldWireControls = true} = {}) {
    applyTheme(getTheme());
    applyI18n(getLanguage());
    if (shouldWireControls) wireControls();
  }

  window.addEventListener("storage", event => {
    if ([LANGUAGE_KEY, LEGACY_LANGUAGE_KEY].includes(event.key)) {
      applyI18n(getLanguage());
    }
    if ([THEME_KEY, LEGACY_THEME_KEY].includes(event.key)) {
      applyTheme(getTheme());
    }
  });

  document.addEventListener("DOMContentLoaded", () => {
    if (document.documentElement.dataset.cprToolsAutoinit !== "false") {
      init();
    }
  });

  window.CPRTools = {
    init,
    getLanguage,
    getTheme,
    setLanguage,
    setTheme,
    applyI18n,
    applyTheme,
    languages,
    locales,
    t
  };

  function moonIcon() {
    return '<svg class="site-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3a6.8 6.8 0 0 0 8.7 8.7A9 9 0 1 1 12 3"></path></svg>';
  }

  function sunIcon() {
    return '<svg class="site-icon" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4"></circle><path d="M12 2v2"></path><path d="M12 20v2"></path><path d="m4.93 4.93 1.41 1.41"></path><path d="m17.66 17.66 1.41 1.41"></path><path d="M2 12h2"></path><path d="M20 12h2"></path><path d="m6.34 17.66-1.41 1.41"></path><path d="m19.07 4.93-1.41 1.41"></path></svg>';
  }
})();
