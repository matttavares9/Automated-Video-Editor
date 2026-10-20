"""A small browser client of the FastAPI editing service."""
import os
from pathlib import Path

import httpx
from flask import Flask, Response, jsonify, render_template_string, request, stream_with_context, url_for

app = Flask(__name__)
API_BASE_URL = os.getenv("API_BASE_URL", "http://127.0.0.1:8000").rstrip("/")

PAGE_STYLES = """
@import url('https://fonts.googleapis.com/css2?family=Montserrat:wght@400;500;600;700;800&display=swap');
:root { color-scheme: light; --ink: #172438; --muted: #68788d; --blue: #4c82c3; --blue-dark: #315f96; --line: #d8e0e9; }
* { box-sizing: border-box; }
body { margin: 0; min-height: 100vh; padding: 56px 22px; background: #edf1f5; color: var(--ink); font: 500 14px/1.65 'Montserrat', sans-serif; text-transform: uppercase; }
main { width: min(100%, 820px); margin: 0 auto; }
.card { padding: clamp(25px, 5vw, 52px); border: 1px solid #e0e6ed; border-radius: 20px; background: #f9fafc; box-shadow: 0 18px 55px #233c5810; }
.eyebrow { margin: 0 0 10px; color: var(--blue); font-size: 11px; font-weight: 700; letter-spacing: .22em; text-align: center; }
h1 { margin: 0; color: #034ab7; font-size: clamp(25px, 5vw, 39px); font-weight: 700; letter-spacing: .075em; line-height: 1.2; text-align: center; }
.description { max-width: 620px; margin: 17px auto 38px; color: var(--muted); font-size: 12px; line-height: 1.9; text-align: center; }
.section-title { margin: 0 0 13px; font-size: 12px; font-weight: 700; letter-spacing: .12em; }
.upload-zone { display: grid; min-height: 235px; place-content: center; justify-items: center; gap: 7px; border: 1px dashed #aeb9c7; border-radius: 15px; background: #eef2f6; color: var(--muted); cursor: pointer; opacity: .72; text-align: center; transition: opacity .18s ease, border-color .18s ease, background .18s ease, transform .18s ease; }
.upload-zone:hover, .upload-zone:focus-within { opacity: 1; border-color: var(--blue); background: #e8eff7; }
.upload-zone:active { transform: scale(.995); }
.plus { color: #8491a1; font: 300 72px/1 Arial, sans-serif; }
.upload-main { color: var(--ink); font-size: 12px; font-weight: 700; letter-spacing: .08em; }
.upload-help, .file-list { max-width: 90%; color: var(--muted); font-size: 10px; }
.visually-hidden { position: absolute; width: 1px; height: 1px; overflow: hidden; clip: rect(0,0,0,0); white-space: nowrap; clip-path: inset(50%); }
.controls { display: grid; grid-template-columns: 1fr 1fr; gap: 22px; margin: 30px 0 27px; }
.control { padding: 18px; border: 1px solid var(--line); border-radius: 13px; background: #fff; }
.control-head { display: flex; align-items: baseline; justify-content: space-between; gap: 8px; }
.control label { font-size: 10px; font-weight: 700; letter-spacing: .055em; }
.readout { color: var(--blue-dark); font-size: 12px; font-weight: 700; white-space: nowrap; }
input[type=range] { width: 100%; margin: 16px 0 4px; accent-color: var(--blue); cursor: pointer; }
.hint { margin: 4px 0 0; color: var(--muted); font-size: 9px; }
button, .action { display: block; width: 100%; padding: 15px 20px; border: 0; border-radius: 10px; background: var(--blue-dark); color: #fff; font: 700 12px 'Montserrat', sans-serif; letter-spacing: .12em; text-align: center; text-decoration: none; text-transform: uppercase; cursor: pointer; transition: background .18s ease, transform .18s ease; }
button:hover, .action:hover { background: #264e7d; transform: translateY(-1px); }
.footnote { margin: 20px 0 0; color: var(--muted); font-size: 9px; line-height: 1.8; text-align: center; }
.result-message { margin: 28px 0; padding: 17px; border: 1px solid var(--line); border-radius: 12px; background: #eef2f6; color: var(--ink); text-align: center; }
.secondary { display: block; margin-top: 18px; color: var(--blue-dark); font-size: 10px; font-weight: 700; letter-spacing: .08em; text-align: center; text-decoration: none; }
.secondary:hover { text-decoration: underline; }
@media (max-width: 620px) { body { padding: 24px 13px; } .controls { grid-template-columns: 1fr; gap: 12px; } .upload-zone { min-height: 200px; } }
"""

PAGE_SHELL = """
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>AUTOMATED VIDEO EDITOR</title>
  <style>{{ styles|safe }}</style>
</head>
<body>{{ body|safe }}</body>
</html>
"""


@app.get("/health")
def health():
    return {"status": "ok", "service": "flask-dashboard"}


@app.get("/")
def index():
    body = render_template_string("""
    <main class="card">
      <h1>AUTOMATED VIDEO EDITOR</h1>
      <p class="description">UPLOAD YOUR CLIPS AND LET THE C++ BLUR-DETECTION ALGORITHM FIND, TRIM, AND COMBINE THE CLEAR SECTIONS AUTOMATICALLY.</p>
      <form action="{{ url_for('edit') }}" method="post" enctype="multipart/form-data">
        <h2 class="section-title">UPLOAD CLIPS</h2>
        <label class="upload-zone" for="clips">
          <input class="visually-hidden" id="clips" name="files" type="file" accept="video/*" multiple required>
          <span class="plus" aria-hidden="true">+</span>
          <span class="upload-main">CLICK TO SELECT VIDEO CLIPS</span>
          <span class="upload-help">MP4, MOV, AVI, MKV, WEBM · MULTIPLE CLIPS SUPPORTED</span>
          <span class="file-list" id="file-list" aria-live="polite"></span>
        </label>
        <div class="controls">
          <section class="control">
            <div class="control-head"><label for="blur-range">BLUR THRESHOLD</label><output class="readout" id="blur-readout">5</output></div>
            <input id="blur-range" type="range" min="0" max="1000" step="any" value="424.742" aria-describedby="blur-hint">
            <input id="blur_threshold" name="blur_threshold" type="hidden" value="5">
            <p class="hint" id="blur-hint">LOWER VALUES ALLOW MORE BLUR · DEFAULT 5</p>
          </section>
          <section class="control">
            <div class="control-head"><label for="duration-range">MAXIMUM COMBINED EDIT LENGTH</label><output class="readout" id="duration-readout">8 SEC</output></div>
            <input id="duration-range" name="max_total_duration_seconds" type="range" min="1" max="600" step="1" value="8">
            <p class="hint">SELECT THE MAXIMUM TOTAL VIDEO LENGTH</p>
          </section>
        </div>
        <button type="submit">AUTO-EDIT CLIPS</button>
      </form>
      <p class="footnote">THIS BROWSER PAGE USES THE SAME FASTAPI SERVICE AS THE MCP TOOLS. THE EDITING ALGORITHM RUNS IN C++.</p>
    </main>
    <script>
      const blurSlider = document.getElementById('blur-range');
      const blurField = document.getElementById('blur_threshold');
      const blurReadout = document.getElementById('blur-readout');
      const durationSlider = document.getElementById('duration-range');
      const durationReadout = document.getElementById('duration-readout');
      const fileInput = document.getElementById('clips');
      const fileList = document.getElementById('file-list');
      function updateBlur() {
        const raw = 0.1 * Math.pow(10000, Number(blurSlider.value) / 1000);
        const value = Number(raw.toPrecision(3));
        blurField.value = value;
        blurReadout.value = String(value);
        blurReadout.textContent = String(value);
      }
      blurSlider.addEventListener('input', updateBlur);
      durationSlider.addEventListener('input', () => {
        durationReadout.value = durationSlider.value + ' SEC';
        durationReadout.textContent = durationSlider.value + ' SEC';
      });
      fileInput.addEventListener('change', () => {
        fileList.textContent = Array.from(fileInput.files, file => file.name).join(' · ');
      });
    </script>
    """)
    return render_template_string(PAGE_SHELL, styles=PAGE_STYLES, body=body)


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
    body = render_template_string("""
    <main class="card">
      <p class="eyebrow">AUTOMATED VIDEO EDITOR</p>
      <h1>EDIT STATUS</h1>
      <p class="result-message">{{ message }}</p>
      {% if download_url %}<a class="action" href="{{ download_url }}">DOWNLOAD EDITED VIDEO</a>{% endif %}
      <a class="secondary" href="{{ url_for('index') }}">EDIT MORE CLIPS</a>
    </main>
    """, message=message, download_url=download_url)
    return render_template_string(PAGE_SHELL, styles=PAGE_STYLES, body=body), status


@app.get("/jobs/<job_id>")
def job(job_id: str):
    response = httpx.get(f"{API_BASE_URL}/jobs/{job_id}", timeout=15)
    return jsonify(response.json()), response.status_code
