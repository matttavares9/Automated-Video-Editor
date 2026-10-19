"""A small browser client of the FastAPI editing service."""
import os
from pathlib import Path

import httpx
from flask import Flask, Response, jsonify, render_template_string, request, stream_with_context, url_for

app = Flask(__name__)
API_BASE_URL = os.getenv("API_BASE_URL", "http://127.0.0.1:8000").rstrip("/")


@app.get("/health")
def health():
    return {"status": "ok", "service": "flask-dashboard"}


@app.get("/")
def index():
    return render_template_string("""
    <!doctype html><title>FastAPI Blur Trimmer</title>
    <h1>FastAPI Blur Trimmer</h1>
    <p>Upload one or more clips. The original C++ algorithm automatically finds
    the sustained clear section in each qualifying clip, trims it, and joins
    the results.</p>
    <form action="{{ url_for('edit') }}" method="post" enctype="multipart/form-data">
      <p><label>Clips <input name="files" type="file" accept="video/*" multiple required></label></p>
      <p><label>Blur threshold <input name="blur_threshold" type="number" value="5" min="0.1" max="1000" step="0.1"></label>
      <small>Lower allows more blur; 5 is the default.</small></p>
      <p><label>Maximum combined edit length (seconds) <input name="max_total_duration_seconds" type="number" value="8" min="1" step="1"></label></p>
      <button type="submit">Edit automatically</button>
    </form>
    <p><small>This Flask page calls the same FastAPI service as the MCP tools; it has no separate editing algorithm.</small></p>
    """)


@app.post("/edit")
def edit():
    files = [file for file in request.files.getlist("files") if file.filename]
    if not files:
        return _result_page("Choose at least one video file.", 400)
    try:
        blur_threshold = float(request.form.get("blur_threshold", 5))
        maximum = float(request.form.get("max_total_duration_seconds", 8))
        uploads = []
        with httpx.Client(timeout=190) as client:
            for file in files:
                response = client.post(
                    f"{API_BASE_URL}/jobs",
                    files={"file": (file.filename, file.stream, file.mimetype or "video/mp4")},
                )
                response.raise_for_status()
                uploads.append(response.json())
            response = client.post(
                f"{API_BASE_URL}/compilations",
                json={
                    "job_ids": [upload["id"] for upload in uploads],
                    "min_clip_seconds": 1,
                    "max_total_duration_seconds": maximum,
                    "blur_threshold": blur_threshold,
                },
                timeout=300,
            )
            response.raise_for_status()
        result = response.json()
        name = Path(result["download_url"]).name
        return _result_page(
            f"Edited {len(result['rendered_job_ids'])} clip(s). "
            f"Skipped {len(result['skipped_job_ids'])} clip(s) without a qualifying clear section.",
            200,
            url_for("download_compilation", output_name=name),
        )
    except (ValueError, httpx.HTTPError) as error:
        detail = error.response.text if isinstance(error, httpx.HTTPStatusError) else str(error)
        return _result_page(f"Editing failed: {detail}", 422)


@app.get("/exports/<output_name>")
def download_compilation(output_name: str):
    """Stream an API export through the browser-facing Flask service."""
    client = httpx.Client(timeout=300)
    upstream = client.send(client.build_request("GET", f"{API_BASE_URL}/exports/{output_name}"), stream=True)
    if upstream.status_code != 200:
        upstream.close()
        client.close()
        return _result_page("The requested export is no longer available.", upstream.status_code)

    @stream_with_context
    def stream():
        try:
            yield from upstream.iter_bytes()
        finally:
            upstream.close()
            client.close()

    return Response(
        stream(),
        content_type=upstream.headers.get("content-type", "video/mp4"),
        headers={"Content-Disposition": f'attachment; filename="{output_name}"'},
    )


def _result_page(message: str, status: int, download_url: str | None = None):
    return render_template_string("""
    <!doctype html><title>FastAPI Blur Trimmer</title>
    <h1>FastAPI Blur Trimmer</h1>
    <p>{{ message }}</p>
    {% if download_url %}<p><a href="{{ download_url }}">Download edited video</a></p>{% endif %}
    <p><a href="{{ url_for('index') }}">Edit more clips</a></p>
    """, message=message, download_url=download_url), status


@app.get("/jobs/<job_id>")
def job(job_id: str):
    response = httpx.get(f"{API_BASE_URL}/jobs/{job_id}", timeout=15)
    return jsonify(response.json()), response.status_code
