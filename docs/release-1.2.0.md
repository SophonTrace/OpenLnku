# OpenLnku 1.2.0

OpenLnku 1.2.0 brings the new name and blue interlocking hexagon icon, a complete Simplified Chinese / English interface, and automatic / domestic / overseas speed-test server selection.

## Changes

- Renamed the application, tray, installer, release assets and update repository to OpenLnku.
- Preserved OpenNex credentials and preferences; migrated startup and installation registration, retained installation paths and removed obsolete managed files and shortcuts.
- Added a persistent language selector to Settings. Interface labels, dynamic status, dialogs and tray actions switch immediately; canonical diagnostic logs remain in Chinese for consistent troubleshooting.
- Added actual application screenshots and bilingual project documentation.
- Retained AGPL-3.0 and existing third-party attribution.

## Usage

Choose the installer for a current-user installation or extract the portable ZIP and run OpenLnku.exe. Windows 10+ x64 is supported. Exit the existing tray client before upgrading. No administrator privileges are needed.

Automatic startup, automatic connection and reconnection default to enabled for new users. Existing explicit preferences are preserved. Other authentication clients must fully exit to free UDP 61440.

Updates use signed ECDSA P-256 manifests and SHA-256 archive verification. The executable and installer do not have Windows Authenticode signatures.

## Verification scope

Offline tests cover protocol, traffic, localization and preference migration, speed-test parsing, signed update validation and controller recovery. Release acceptance separately checks a deployed package without the Qt SDK in PATH, native Windows tray behavior, current-user installation, repair, upgrade, uninstallation and update rollback.

- Six offline CTest groups passed.
- Runtime Chinese / English switching and tray label checks passed.
- Standalone deployment passed with Qt SDK paths excluded.
- 51 installation, upgrade, repair, native tray and uninstall assertions passed.
- 45 isolated brand migration assertions passed, including missing legacy startup preferences, stale same-path startup removal and foreign-path startup preservation.
- Eight Windows PowerShell 5.1 update-helper scenarios passed, including byte-for-byte rollback, damaged archive rejection, unsafe paths, conflicting user files and interrupted-update recovery.

Screenshots show the actual Qt application with clearly labeled synthetic demo data. Earlier real network probes exercised domestic physical-adapter and overseas system-route transfers; they do not establish compatibility with other schools or every proxy configuration.
