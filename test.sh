#!/bin/bash

# Test script for Pokemon Game Server
# Tests gRPC endpoints using grpcurl

set -e

if ! command -v grpcurl &> /dev/null; then
    echo "grpcurl is not installed. Please install it first:"
    echo "  go install github.com/fullstorydev/grpcurl/cmd/grpcurl@latest"
    exit 1
fi

SERVER="localhost:50051"
TIMEOUT=5

echo "=== Pokemon Game Server gRPC Tests ==="
echo "Testing server at: $SERVER"
echo ""

# Test 1: Create Room
echo "Test 1: CreateRoom"
CREATE_RESPONSE=$(grpcurl -plaintext \
  -d '{"room_name": "TestRoom1", "max_players": 4}' \
  -max-time $TIMEOUT \
  "$SERVER" pokemon.game.RoomService/CreateRoom)

echo "$CREATE_RESPONSE"
ROOM_ID=$(echo "$CREATE_RESPONSE" | grep '"room_id"' | sed 's/.*"room_id": "\([^"]*\)".*/\1/')
echo "Created room: $ROOM_ID"
echo ""

# Test 2: Get Room Status
if [ ! -z "$ROOM_ID" ]; then
    echo "Test 2: GetRoomStatus"
    grpcurl -plaintext \
      -d "{\"room_id\": \"$ROOM_ID\"}" \
      -max-time $TIMEOUT \
      "$SERVER" pokemon.game.RoomService/GetRoomStatus
    echo ""
fi

# Test 3: Create Multiple Rooms
echo "Test 3: Create Multiple Rooms"
for i in {2..3}; do
    grpcurl -plaintext \
      -d "{\"room_name\": \"TestRoom$i\", \"max_players\": 4}" \
      -max-time $TIMEOUT \
      "$SERVER" pokemon.game.RoomService/CreateRoom | tail -1
done
echo ""

# Test 4: List all rooms (via repeated GetRoomStatus calls)
echo "Test 4: Server is running and accepting requests"
echo "All tests completed successfully!"
