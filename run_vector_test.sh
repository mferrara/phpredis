#!/bin/bash

# Start Redis Vector container if not already running
REDIS_CONTAINER_NAME="redis-vector"
if ! docker ps | grep -q $REDIS_CONTAINER_NAME; then
    echo "Starting Redis 8 container with Vector module..."
    docker run -d --name $REDIS_CONTAINER_NAME -p 6379:6379 \
        redis/redis-stack:latest
    
    # Give Redis a moment to start
    sleep 3
fi

# Build the PHP container with our improved Vector Sets implementation
echo "Building PHP container with Vector Sets implementation..."
docker build --no-cache -t phpredis-vector-sets -f Dockerfile.feature .

# Create custom network if not exists
NETWORK_NAME="redis-phpredis-network"
if ! docker network ls | grep -q $NETWORK_NAME; then
    echo "Creating custom network $NETWORK_NAME"
    docker network create $NETWORK_NAME
fi

# Connect Redis container to our custom network if needed
if ! docker network inspect $NETWORK_NAME | grep -q "$REDIS_CONTAINER_NAME"; then
    echo "Connecting Redis container to $NETWORK_NAME"
    docker network connect $NETWORK_NAME $REDIS_CONTAINER_NAME || true
fi

# Run the container with a name, connected to the same network as Redis container
echo "Running PHP container connected to $NETWORK_NAME"
docker run --rm -it --name phpredis-test --network $NETWORK_NAME \
    -e REDIS_HOST=$REDIS_CONTAINER_NAME \
    phpredis-vector-sets
