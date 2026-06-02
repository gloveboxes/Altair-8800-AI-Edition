# Dockerised `altair_local`

This folder packages the local Altair runner as a small multi-stage Alpine image.
The container always serves the **browser terminal**: it serves the web terminal
UI over HTTP and bridges it to the emulator over a WebSocket, so you run it
detached and connect from a browser. The Dockerfile is buildx-ready for both
`linux/amd64` and `linux/arm64`.

## Quick start (prebuilt image)

A prebuilt multi-arch image is published on Docker Hub as
[`glovebox/altair8800v2`](https://hub.docker.com/r/glovebox/altair8800v2):

```sh
docker run -d --name altair8800v2 -p 8080:8080 glovebox/altair8800v2:latest
```

Then open <http://localhost:8080/> in a browser. Stop it with
`docker stop altair8800v2` (and `docker rm altair8800v2` to remove). To bind-mount
your own disks/apps, see [Run (detached)](#run-detached) below.

> The browser terminal is backed by the
> [wsServer](https://github.com/Theldus/wsServer) git submodule. If you build the
> image yourself, initialise it first so it is present in the build context:
>
> ```sh
> git submodule update --init altair_local/external/wsServer
> ```
>
> If the submodule is missing the image still builds, but the browser terminal is
> unavailable and the container falls back to the stdio terminal.

## Build

```sh
docker build -f docker/Dockerfile -t glovebox/altair8800v2:latest .
```

## Build multi-architecture images

To build and push a combined `linux/amd64` + `linux/arm64` image to Docker Hub,
log in first and either set your Docker Hub username or let the script prompt for it:

```sh
docker login
./docker/build-multiarch.sh
```

The helper script creates or reuses a buildx builder, bootstraps it, and pushes
the multi-architecture manifest list to your Docker account.

Optional environment variables:

- `DOCKER_USER` sets the Docker Hub namespace when `DOCKER_IMAGE` is not provided.
- `IMAGE_NAME` defaults to `altair8800v2`.
- `DOCKER_TAG` defaults to `latest`.
- `DOCKER_IMAGE` overrides the fully-qualified image reference directly.
- `PLATFORMS` defaults to `linux/amd64,linux/arm64`.
- `BUILDER_NAME` defaults to `altair8800v2-multiarch`.
- `DOCKERFILE` defaults to `docker/Dockerfile`.
- `BUILD_CONTEXT` defaults to the repository root.

The helper always builds with `--no-cache`.

Example with an explicit tag:

```sh
DOCKER_USER=your-dockerhub-user DOCKER_TAG=2026-05-28 ./docker/build-multiarch.sh
```

Example overriding the whole image reference directly:

```sh
DOCKER_IMAGE=your-dockerhub-user/altair8800v2:2026-05-28 ./docker/build-multiarch.sh
```

## Run (detached)

The container always serves the browser terminal, so run it detached and connect
from a browser. No TTY is required.

```sh
docker run -d --name altair8800v2 \
  -p 8080:8080 \
  -v "$PWD/docker/altair_env.txt:/opt/altair/runtime/altair_env.txt:ro" \
  -v "$PWD/Apps:/opt/altair/Apps:ro" \
  -v "$PWD/disks:/opt/altair/disks" \
  glovebox/altair8800v2:latest
```

Then open <http://localhost:8080/> in a browser. The emulator waits to boot until
the first browser connects, then streams the CP/M banner to the page. Stop it with
`docker stop altair8800v2` (and `docker rm` to remove).

Use a different port with `ALTAIR_WEB_PORT` (publish the matching `-p`):

```sh
docker run -d --name altair8800v2 -e ALTAIR_WEB_PORT=9000 -p 9000:9000 \
  -v "$PWD/disks:/opt/altair/disks" \
  glovebox/altair8800v2:latest
```

The entrypoint always passes explicit runtime paths:

- `ALTAIR_APPS_ROOT` selects the Apps folder used by FT file lookups.
- `ALTAIR_ENV_FILE` selects the mapped `altair_env.txt`.
- `ALTAIR_DISKS_DIR` points at the mounted disks directory.
- `ALTAIR_DRIVE_A` through `ALTAIR_DRIVE_D` override filenames inside that directory.
- `ALTAIR_DRIVE_A_PATH` through `ALTAIR_DRIVE_D_PATH` override each drive with a full path.
- `ALTAIR_WEB_PORT` selects the served/published port (default `8080`).
- `ALTAIR_WEB_ROOT` locates the bundled terminal UI (default `/opt/altair`); rarely
  needs changing.

By default the image includes a copy of the repo `Apps` folder, but FT is usually
more useful if you bind-mount your working Apps tree over `/opt/altair/Apps`.

Example with an external env file and an alternate disk folder:

```sh
docker run -d --name altair8800v2 -p 8080:8080 \
  -v /absolute/path/to/altair_env.txt:/opt/altair/runtime/altair_env.txt:ro \
  -v /absolute/path/to/Apps:/opt/altair/Apps:ro \
  -v /absolute/path/to/disks:/opt/altair/disks \
  glovebox/altair8800v2:latest
```

Example overriding only drive B while keeping the default disk directory mount:

```sh
docker run -d --name altair8800v2 -p 8080:8080 \
  -v "$PWD/docker/altair_env.txt:/opt/altair/runtime/altair_env.txt:ro" \
  -v "$PWD/Apps:/opt/altair/Apps:ro" \
  -v "$PWD/disks:/opt/altair/disks" \
  -e ALTAIR_DRIVE_B=my-workbench.dsk \
  glovebox/altair8800v2:latest
```

Example using a different Apps folder for FT without changing the container path:

```sh
docker run -d --name altair8800v2 -p 8080:8080 \
  -v /absolute/path/to/other-apps:/opt/altair/Apps:ro \
  -v "$PWD/docker/altair_env.txt:/opt/altair/runtime/altair_env.txt:ro" \
  -v "$PWD/disks:/opt/altair/disks" \
  glovebox/altair8800v2:latest
```

## Compose

`compose.yaml` runs the container detached with the browser terminal published on
port 8080:

```sh
docker compose -f docker/compose.yaml up -d
```

Then open <http://localhost:8080/>. Stop it with
`docker compose -f docker/compose.yaml down`.

Override the mounted resources without editing the compose file:

```sh
ALTAIR_ENV_FILE=/absolute/path/to/altair_env.txt \
ALTAIR_APPS_DIR=/absolute/path/to/Apps \
ALTAIR_DISKS_DIR=/absolute/path/to/disks \
docker compose -f docker/compose.yaml run --rm altair8800v2
```

## Verify a single target platform locally

If you want to test one non-native Linux target before pushing a multi-arch image,
build a single-platform image with buildx and load it into your local Docker image store:

```sh
docker buildx build --platform linux/amd64 -f docker/Dockerfile -t altair8800v2:amd64 --load .
```
