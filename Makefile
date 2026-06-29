.PHONY: up down targets compare clean

up:
	docker compose up -d --wait

down:
	docker compose down

targets:
	./shared/gen_targets.sh

compare:
	./scripts/compare.sh

clean:
	rm -rf results/*
