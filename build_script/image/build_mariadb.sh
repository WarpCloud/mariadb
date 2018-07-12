#!/bin/bash
function build_mariadb
## The function name should be the same as script's name
{
    set -e

    # 1.if $KUNDB_SRC exists, cd
    if [ $MARIA_SRC ]; then
        cd $MARIA_SRC
        chmod -R o=g *
    else
        echo "MARIA_SRC not set, exit"
        exit 1
    fi

    # 2. docker build
    docker build -f docker/Dockerfile -t maria .

    # 3. rename image && push
    docker tag maria:latest ${DOCKER_REPO_URL}/${BUILDER}/${COMPONENT_BASE}:${IMAGE_TAG}
    docker push ${DOCKER_REPO_URL}/${BUILDER}/${COMPONENT_BASE}:${IMAGE_TAG}
}
