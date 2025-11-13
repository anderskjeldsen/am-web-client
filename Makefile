# place amlc.jar in this folder or change value.
AMLC=amlc.jar
CMD=java -jar $(AMLC)

clean:
	$(CMD) clean . -bt amigaos_docker

build:
	$(CMD) build . -bt amigaos_docker -fld -maxOneError

build-rl:
	$(CMD) build . -bt amigaos_docker -rl

build-force-deps:
	$(CMD) build . -fld -bt amigaos_docker

test:
	$(CMD) test . -bt amigaos_docker
