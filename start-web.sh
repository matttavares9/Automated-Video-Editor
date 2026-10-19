#!/bin/sh
set -eu

# Keep the editing API private inside the task/container.  Flask is the sole
# process bound to the Elastic Beanstalk-facing port.
uvicorn api.app.main:app --host 127.0.0.1 --port 8001 &
api_pid=$!

gunicorn flask_dashboard.app:app \
  --bind 0.0.0.0:8000 \
  --workers "${WEB_CONCURRENCY:-2}" \
  --timeout "${WEB_TIMEOUT_SECONDS:-360}" &
web_pid=$!

shutdown() {
  kill -TERM "$web_pid" "$api_pid" 2>/dev/null || true
  wait "$web_pid" "$api_pid" 2>/dev/null || true
}

trap shutdown INT TERM

# If either process stops, shut the other one down so the container is marked
# unhealthy instead of serving a dashboard with no editing backend.
while kill -0 "$api_pid" 2>/dev/null && kill -0 "$web_pid" 2>/dev/null; do
  sleep 1
done

shutdown
exit 1
