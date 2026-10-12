<p align="center"><img src="assets/branding/mark.svg" width="88" alt="OpenLnku logo"></p>
<h1 align="center">OpenLnku</h1>
<p align="center">Connect quickly. Reconnect automatically.</p>
<p align="center"><a href="https://github.com/SophonTrace/OpenLnku/releases/latest">Download for Windows</a> · <a href="README.md">简体中文</a> · <a href="https://github.com/SophonTrace/OpenLnku/issues">Report an issue</a></p>

OpenLnku is a Windows campus network authentication client with a simple blue and white interface. Choose your adapter, enter your credentials and connect. See authentication status, traffic and speed tests in one place, or handle everyday actions from the tray.

Currently verified on the **Jilin University campus network (JLU / DrCOM)**. Other universities have not been verified. This independent open-source project is not affiliated with the university or its official client.

![OpenLnku connection overview](docs/images/overview.png)

*Actual-use screenshots in Chinese. Account details, the password field and network addresses are redacted.*

## Features

| Feature | What it offers |
| --- | --- |
| Modern interface | Blue and white cards, clear connection status, live charts and high-DPI support |
| Adapter selection and binding | Choose the adapter connected to your campus network and bind authentication to it |
| Automatic reconnection | Attempt to restore authentication after a connection drop or suspend/resume |
| Launch at startup | Start with Windows and connect using saved credentials |
| Speed tests | Download, upload, HTTP latency, jitter and charts, with a base scale of 1200 Mbps |
| Two test paths | Compare system routing with direct access through your physical adapter |
| Test options | Automatic/domestic/overseas servers, automatic/IPv4/IPv6, standard/extended duration |
| Exit information | Country flag, public IP, provider, approximate location and time zone; copy or hide the IP |
| DNS leak check | See DNS resolvers actually observed by the detection service and their location information |
| Traffic statistics | Monitor the selected adapter's throughput and totals for the current app session |
| Tray and languages | Connect, disconnect or exit from the tray; Simplified Chinese and English |
| In-app updates | Check for and download new versions while keeping account details and preferences |

## Download and connect

Supports **64-bit Windows 10 and 11**. Normal use does not require administrator privileges.

1. Get the installer or portable ZIP from [Releases](https://github.com/SophonTrace/OpenLnku/releases/latest). Extract the portable ZIP completely. Fully exit any other authentication client, including its tray process.
2. Open OpenLnku and choose your campus network adapter on the Network page.
3. Enter your credentials on Overview, select Connect and wait for authentication confirmation.

Startup, automatic connection and automatic reconnection are enabled by default for new users. Adjust them in Settings. Automatic connection requires saved credentials. Manually disconnecting pauses reconnection until you connect again.

Closing the window keeps the app running in the tray. To close it completely, select Exit OpenLnku in the tray menu. No campus information popup is opened after connecting.

## Choosing a speed test

![OpenLnku speed test](docs/images/network.png)

**System routing** measures your current Windows network path, including a system proxy or TUN. **Physical adapter** uses the selected adapter directly. A binding failure is reported rather than silently switching to the system path.

Open Test options to select a server group, IP protocol and duration:

- **Automatic server:** uses the exit country to prefer domestic servers for mainland China and Cloudflare elsewhere, with fallback if the preferred group is unavailable.
- **Domestic/overseas:** domestic tests use Shanghai University; overseas tests use Cloudflare. Cloudflare chooses its location, and the app shows the observed colo code.
- **IP protocol:** choose automatic, IPv4 or IPv6. Their network paths may differ. An explicit system proxy may control the target protocol; the app reports when it cannot guarantee the requested family.
- **Duration:** standard measurements use 10 seconds for download and 7 for upload; extended mode uses 20 and 15 seconds. Connections warm up before measurement.

Tests consume network data; the panel shows the amount used. Results measure the path to the selected server. Server load, routing, IP protocol and proxies all affect speed, so one result does not describe every website.

## Exit information and DNS checks

The exit card shows the public address observed by the lookup service through the selected test path, with an approximate location, provider and time zone. Proxy rules may route other websites through different exits.

DNS checks support system DNS and the selected adapter's configured resolvers. With Fake-IP, supplementary connection-triggered observations are labelled separately. Missing or partial records are marked inconclusive. Matching regions do not prove no leak, and different regions do not necessarily mean a leak. Browser-specific secure DNS is outside this check.

Exit lookups and DNS checks use Cloudflare / IPLeak services, which can observe query exits or resolver addresses. **Campus credentials are never sent to these services.**

## Accounts, updates and uninstalling

![OpenLnku settings](docs/images/settings.png)

- Saved passwords use Windows data protection for the current user. Uncheck Remember password or clear the account in Settings to remove stored credentials.
- Check for updates in Settings, then follow the download and restart prompts. Exit the old tray client before a manual upgrade. Account details and preferences are retained.
- Existing OpenNex account details and settings migrate automatically.
- Uninstalling keeps account details and preferences by default, with an option to remove them.
- The application and installer currently have no Windows code-signing certificate.

Traffic totals include other applications using the selected adapter. They start when OpenLnku opens and reset when it restarts.

## Common questions

**Cannot authenticate?** Fully exit other authentication clients and check the selected adapter, credentials and Ethernet/Wi-Fi connection. You can export logs from the Logs page.

**Authenticated, but websites do not open?** Authentication and internet access are separate checks. Run the internet access check, then check your proxy and DNS settings. OpenLnku does not automatically change the global proxy configuration.

**Why are IPv4 and IPv6 speeds different?** They may use different routes, servers or proxy rules. Compare them on the same test path and server group, using extended mode if needed.

## Feedback and source

Report problems in [Issues](https://github.com/SophonTrace/OpenLnku/issues) with the app version, Windows version, connection type and steps to reproduce. Hide account details, IP and MAC addresses, and personal location information before sharing screenshots or logs.

Licensed under [AGPL-3.0](LICENSE). See [third-party notices](THIRD_PARTY.md) and the [build guide](docs/BUILD.md).
