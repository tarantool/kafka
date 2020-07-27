NETWORK="tnt-kafka-tests"

docker-remove-network:
	docker network remove ${NETWORK} || true

docker-create-network: docker-remove-network
	docker network create ${NETWORK}

docker-remove-zoo:
	docker rm -f zookeeper || true

docker-run-zoo: docker-remove-zoo
	docker run -d \
        --net=${NETWORK} \
        --name=zookeeper \
        -p 2181:2181 \
        -e ZOOKEEPER_CLIENT_PORT=2181 \
        confluentinc/cp-zookeeper:5.0.0

docker-remove-kafka:
	docker rm -f kafka --volumes || true

docker-pull-kafka:
	docker pull wurstmeister/kafka

docker-run-kafka: docker-remove-kafka
	docker run -d \
        --net=${NETWORK} \
        --name=kafka \
        -p 9092:9092 \
        -e KAFKA_ZOOKEEPER_CONNECT=zookeeper:2181 \
        -e KAFKA_LISTENERS=PLAINTEXT://0.0.0.0:9092 \
        -e KAFKA_ADVERTISED_LISTENERS=PLAINTEXT://kafka:9092 \
        -e KAFKA_OFFSETS_TOPIC_REPLICATION_FACTOR=1 \
        wurstmeister/kafka

docker-read-topic-data:
	docker run \
      --net=${NETWORK} \
      --rm \
      confluentinc/cp-kafka:5.0.0 \
      kafka-console-consumer --bootstrap-server kafka:9092 --topic test_partially_unsubscribe_1 --from-beginning

APP_NAME = kafka-test
APP_IMAGE = kafka-test-image

docker-build-app:
	docker build -t ${APP_IMAGE} -f ./docker/Dockerfile .

docker-remove-app:
	docker rm -f ${APP_NAME} || true

docker-run-app: docker-build-app docker-remove-app
	docker run -d \
		-p 3301:3301 \
		--net ${NETWORK} \
		--name ${APP_NAME} \
		-e KAFKA_BROKERS=kafka:9092 \
		${APP_IMAGE}

docker-run-interactive: docker-build-app docker-remove-app
	docker run -it \
		-p 3301:3301 \
		--net ${NETWORK} \
		--name ${APP_NAME} \
		-e KAFKA_BROKERS=kafka:9092 \
		${APP_IMAGE}

docker-remove-all: \
	docker-remove-app \
	docker-remove-kafka \
	docker-remove-zoo \
	docker-remove-network

docker-run-environment: \
	docker-remove-all \
	docker-create-network \
	docker-run-zoo \
	docker-run-kafka

docker-run-all: \
	docker-run-environment \
	docker-create-network \
	docker-build-app \
	docker-run-app

#######################################################################
# Tests

tests-dep:
	cd ./tests && \
        python3 -m venv venv && \
		. venv/bin/activate && \
		pip install -r requirements.txt && \
    	deactivate

tests-run:
	cd ./tests && \
		. venv/bin/activate && \
		pytest -W ignore -vv && \
		deactivate

test-run-with-docker: tests-dep docker-run-all
	sleep 10

	docker run \
		--net=${NETWORK} \
		--rm confluentinc/cp-kafka:5.0.0 \
		kafka-topics --create --topic test_producer --partitions 1 --replication-factor 1 \
		--if-not-exists --zookeeper zookeeper:2181

	docker run \
		--net=${NETWORK} \
		--rm confluentinc/cp-kafka:5.0.0 \
		kafka-topics --create --topic test_consume --partitions 1 --replication-factor 1 \
		--if-not-exists --zookeeper zookeeper:2181

	docker run \
		--net=${NETWORK} \
		--rm confluentinc/cp-kafka:5.0.0 \
		kafka-topics --create --topic test_unsubscribe --partitions 1 --replication-factor 1 \
		--if-not-exists --zookeeper zookeeper:2181

	docker run \
		--net=${NETWORK} \
		--rm confluentinc/cp-kafka:5.0.0 \
		kafka-topics --create --topic test_unsub_partially_1 --partitions 1 --replication-factor 1 \
		--if-not-exists --zookeeper zookeeper:2181

	docker run \
		--net=${NETWORK} \
		--rm confluentinc/cp-kafka:5.0.0 \
		kafka-topics --create --topic test_unsub_partially_2 --partitions 1 --replication-factor 1 \
		--if-not-exists --zookeeper zookeeper:2181

	docker run \
		--net=${NETWORK} \
		--rm confluentinc/cp-kafka:5.0.0 \
		kafka-topics --create --topic test_multi_consume_1 --partitions 1 --replication-factor 1 \
		--if-not-exists --zookeeper zookeeper:2181

	docker run \
		--net=${NETWORK} \
		--rm confluentinc/cp-kafka:5.0.0 \
		kafka-topics --create --topic test_multi_consume_2 --partitions 1 --replication-factor 1 \
		--if-not-exists --zookeeper zookeeper:2181

	docker run \
		--net=${NETWORK} \
		--rm confluentinc/cp-kafka:5.0.0 \
		kafka-topics --create --topic test_consuming_from_last_committed_offset --partitions 1 --replication-factor 1 \
		--if-not-exists --zookeeper zookeeper:2181

	cd ./tests && \
		. venv/bin/activate && \
		pytest -W ignore -vv && \
		deactivate

#######################################################################
# Benchmarks

docker-create-benchmark-async-producer-topic:
	docker run \
		--net=${NETWORK} \
		--rm confluentinc/cp-kafka:5.0.0 \
		kafka-topics --create --topic async_producer_benchmark --partitions 2 --replication-factor 1 \
		--if-not-exists --zookeeper zookeeper:2181

docker-run-benchmark-async-producer-interactive: docker-build-app docker-remove-app
	docker run -it \
		-p 3301:3301 \
		--net ${NETWORK} \
		--name ${APP_NAME} \
		--entrypoint "tarantool" \
		-e KAFKA_BROKERS=kafka:9092 \
		${APP_IMAGE} \
		/opt/tarantool/benchmarks/async_producer.lua

docker-read-benchmark-async-producer-topic-data:
	docker run \
      --net=${NETWORK} \
      --rm \
      confluentinc/cp-kafka:5.0.0 \
      kafka-console-consumer --bootstrap-server kafka:9092 --topic async_producer_benchmark --from-beginning

docker-create-benchmark-sync-producer-topic:
	docker run \
		--net=${NETWORK} \
		--rm confluentinc/cp-kafka:5.0.0 \
		kafka-topics --create --topic sync_producer_benchmark --partitions 2 --replication-factor 1 \
		--if-not-exists --zookeeper zookeeper:2181

docker-run-benchmark-sync-producer-interactive: docker-build-app docker-remove-app
	docker run -it \
		-p 3301:3301 \
		--net ${NETWORK} \
		--name ${APP_NAME} \
		--entrypoint "tarantool" \
		-e KAFKA_BROKERS=kafka:9092 \
		${APP_IMAGE} \
		/opt/tarantool/benchmarks/sync_producer.lua

docker-read-benchmark-sync-producer-topic-data:
	docker run \
		--net=${NETWORK} \
		--rm \
		confluentinc/cp-kafka:5.0.0 \
		kafka-console-consumer --bootstrap-server kafka:9092 --topic sync_producer_benchmark --from-beginning

docker-create-benchmark-auto-offset-store-consumer-topic:
	docker run \
		--net=${NETWORK} \
		--rm confluentinc/cp-kafka:5.0.0 \
		kafka-topics --create --topic auto_offset_store_consumer_benchmark --partitions 2 --replication-factor 1 \
		--if-not-exists --zookeeper zookeeper:2181

docker-run-benchmark-auto-offset-store-consumer-interactive: docker-build-app docker-remove-app
	docker run -it \
		-p 3301:3301 \
		--net ${NETWORK} \
		--name ${APP_NAME} \
		--entrypoint "tarantool" \
		-e KAFKA_BROKERS=kafka:9092 \
		${APP_IMAGE} \
		/opt/tarantool/benchmarks/auto_offset_store_consumer.lua

docker-read-benchmark-auto-offset-store-consumer-topic-data:
	docker run \
		--net=${NETWORK} \
		--rm \
		confluentinc/cp-kafka:5.0.0 \
		kafka-console-consumer --bootstrap-server kafka:9092 --topic auto_offset_store_consumer_benchmark --from-beginning

docker-create-benchmark-manual-commit-consumer-topic:
	docker run \
		--net=${NETWORK} \
		--rm confluentinc/cp-kafka:5.0.0 \
		kafka-topics --create --topic manual_offset_store_consumer --partitions 2 --replication-factor 1 \
		--if-not-exists --zookeeper zookeeper:2181

docker-run-benchmark-manual-commit-consumer-interactive: docker-build-app docker-remove-app
	docker run -it \
		-p 3301:3301 \
		--net ${NETWORK} \
		--name ${APP_NAME} \
		--entrypoint "tarantool" \
		-e KAFKA_BROKERS=kafka:9092 \
		${APP_IMAGE} \
		/opt/tarantool/benchmarks/manual_offset_store_consumer.lua

docker-read-benchmark-manual-commit-consumer-topic-data:
	docker run \
		--net=${NETWORK} \
		--rm \
		confluentinc/cp-kafka:5.0.0 \
		kafka-console-consumer --bootstrap-server kafka:9092 --topic manual_offset_store_consumer --from-beginning
