#!/bin/bash

docker-compose build

# Start the container in detached mode
docker-compose up -d

# Attach to the container interactively
docker-compose exec cubeboot /bin/bash
