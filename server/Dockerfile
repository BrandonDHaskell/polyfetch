# ---- build ----
FROM golang:1.24-alpine AS build
WORKDIR /src
COPY go.mod ./
RUN go mod download
COPY . .
RUN CGO_ENABLED=0 go build -ldflags="-s -w" -o /out/polyfetch-server ./...

# ---- runtime ----
FROM scratch
COPY --from=build /out/polyfetch-server /polyfetch-server
EXPOSE 8080
ENTRYPOINT ["/polyfetch-server"]
