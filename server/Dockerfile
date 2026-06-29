FROM golang:1.24-alpine AS builder
WORKDIR /build
COPY go.mod main.go ./
RUN go build -o server .

FROM alpine:3.21
COPY --from=builder /build/server /server
EXPOSE 8080
ENTRYPOINT ["/server"]
