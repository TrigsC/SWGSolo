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

docker compose up -d
docker exec -it ollama_brain ollama pull llama3.2

cd SWGEmu/SWGSolo/docker/
source ./env-base
source ./env-run
docker start -ai "$GALAXY_NAME"

## Install ollama for AI
docker network create swg-net

docker run -d \
  --name ollama-brain \
  --network swg-net \
  --restart always \
  -v ollama:/root/.ollama \
  -p 11434:11434 \
  ollama/ollama

curl -fsSL https://ollama.com/install.sh | sh

sudo systemctl edit ollama.service

[Service]
Environment="OLLAMA_HOST=0.0.0.0"

sudo systemctl daemon-reload
sudo systemctl restart ollama

ollama pull llama3.2

- Test
curl http://localhost:11434/api/generate -d '{
  "model": "llama3.2",
  "prompt": "You are a Star Wars padawan. Reply to: Hello there!",
  "stream": false
}'