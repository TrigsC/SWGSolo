# SWGSolo Docker (TrigsC/SWGSolo)

These scripts build a headless SWGEmu container from the [TrigsC/SWGSolo](https://github.com/TrigsC/SWGSolo) fork. Run `./build.sh` once to create the image, then `./run.sh` to launch the galaxy service.

## Published ports
The container publishes the LAN ports required by SWGEmu clients:

| Service | Protocol | Port |
| --- | --- | --- |
| Login | UDP | 44453 |
| REST API | TCP | 44443 |
| Status | TCP | 44455 |
| Ping | UDP | 44462 |
| Zone | UDP | 44463 |
| Web (HTTP) | TCP | 44480 |

If you customise `env-run`, keep these mappings intact so other devices on your network can connect.
