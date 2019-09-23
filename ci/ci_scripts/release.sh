#!/usr/bin/env bash
echo "tag gold image, push to 172.16.1.99/kundb repo."
${CI_SRC}/startdocker.sh &
mkdir -p ~/.docker && cp /root/.docker/config.json ~/.docker/ || true
trap "kill -9 $(ps aux | grep dockerd | grep -v grep | awk '{print $2}')" ERR
sleep $(echo ${SECOND_WAIT_DOCKERD})
export SOURCE="gold"
export TARGET="kundb"
ARCH=$(uname -i)
# tag and push kundb
docker pull ${DOCKER_REPO_URL}/${SOURCE}/${COMPONENT_BASE}:${ARCH}-${BRANCH_NAME}
docker tag ${DOCKER_REPO_URL}/${SOURCE}/${COMPONENT_BASE}:${ARCH}-${BRANCH_NAME} ${DOCKER_REPO_URL}/${TARGET}/${ARCH}/${COMPONENT_BASE}:${BRANCH_NAME}
docker push ${DOCKER_REPO_URL}/${TARGET}/${ARCH}/${COMPONENT_BASE}:${BRANCH_NAME}
# stop docker in docker
echo "kill dockerd before exit"
kill -9 $(ps aux | grep dockerd | grep -v grep | awk '{print $2}')
