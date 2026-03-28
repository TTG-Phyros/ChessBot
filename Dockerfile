FROM gcc:14 AS builder

WORKDIR /app

RUN apt-get update \
	&& apt-get install -y --no-install-recommends make pkg-config libsdl2-dev libsdl2-ttf-dev libmicrohttpd-dev libjson-c-dev \
	&& rm -rf /var/lib/apt/lists/*

COPY . .

RUN make chess-api

FROM alpine:latest

RUN apk add --no-cache libmicrohttpd json-c libc6-compat

WORKDIR /app

COPY --from=builder /app/chess-api .

EXPOSE 5000

CMD ["./chess-api"]
