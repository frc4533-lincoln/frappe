


shell:docker/Dockerfile.shell
	docker buildx build -f docker/Dockerfile.shell -t shell .

shell2:docker/Dockerfile.shell2
	docker buildx build --network=host -f docker/Dockerfile.shell2 -t shell2 .


cross:docker/Dockerfile.cross
	docker run --rm --privileged aptman/qus -- -r
	docker run --rm --privileged aptman/qus -s -- -p arm
	docker buildx build -f docker/Dockerfile.cross -t cross .

rpi0:
	docker buildx build --platform linux/x86_64 -f docker/Dockerfile.rpi0 -t rpi0_build .

resize:
	docker buildx build -f docker/Dockerfile.resize -t resize .

run_rpi0:
	docker run -it --rm --platform linux/x86_64 --privileged -v $$PWD/images:/images -v $$PWD:/root/data rpi0_build


run_resize:
	docker run -it --rm --privileged -v $$PWD/images:/images resize