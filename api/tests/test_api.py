import os
import tempfile
from pathlib import Path
from unittest.mock import patch

os.environ["DATABASE_URL"] = "sqlite:///./test_video_editor.db"
os.environ["DATA_DIR"] = tempfile.mkdtemp(prefix="video-editor-tests-")

from fastapi.testclient import TestClient
from api.app.db import Base, engine
from api.app.engine import DATA_DIR
from api.app.main import app
from api.app.main import validate_edit_plan


client = TestClient(app)


def setup_function():
    Base.metadata.drop_all(engine)
    Base.metadata.create_all(engine)


def test_upload_analyze_status_and_export():
    response = client.post("/jobs", files={"file": ("sample.mp4", b"not-a-real-video", "video/mp4")})
    assert response.status_code == 201
    job_id = response.json()["id"]

    analysis = {
        "status": "ok", "analysis": {"duration_seconds": 12},
        "cuts": [{"start_seconds": 1, "end_seconds": 6, "score": 80, "reason": "sharp"}],
    }
    with patch("api.app.main.run_engine", return_value=analysis):
        response = client.post(f"/jobs/{job_id}/analyze", json={"min_clip_seconds": 2, "max_total_duration_seconds": 10})
    assert response.status_code == 200
    assert response.json()["status"] == "analyzed"
    assert len(response.json()["cuts"]) == 1

    with patch("api.app.main.run_engine", return_value={"status": "ok"}):
        response = client.post(f"/jobs/{job_id}/export", json={"output_name": "final.mp4"})
    assert response.status_code == 200
    assert response.json()["status"] == "exported"


def test_rejects_non_video_upload():
    response = client.post("/jobs", files={"file": ("notes.txt", b"no", "text/plain")})
    assert response.status_code == 415


def test_analysis_passes_blur_threshold_to_engine():
    upload = client.post("/jobs", files={"file": ("sample.mp4", b"not-a-real-video", "video/mp4")})
    job_id = upload.json()["id"]
    result = {"status": "ok", "analysis": {"duration_seconds": 12}, "cuts": []}
    with patch("api.app.main.run_engine", return_value=result) as run_engine:
        response = client.post(f"/jobs/{job_id}/analyze", json={"blur_threshold": 30})
    assert response.status_code == 200
    assert run_engine.call_args.args[1]["blur_threshold"] == 30


def test_compilation_analyzes_and_joins_every_uploaded_clip():
    first = client.post("/jobs", files={"file": ("first.mp4", b"first", "video/mp4")}).json()["id"]
    second = client.post("/jobs", files={"file": ("second.mp4", b"second", "video/mp4")}).json()["id"]
    analysis = {
        "status": "ok", "analysis": {"duration_seconds": 12},
        "cuts": [{"start_seconds": 1, "end_seconds": 6, "score": 0, "reason": "clear"}],
    }
    render = {"status": "ok", "output_path": "/tmp/combined.mp4", "frames_written": 300}
    with patch("api.app.main.run_engine", side_effect=[analysis, analysis, render]) as run_engine:
        response = client.post("/compilations", json={"job_ids": [first, second]})
    assert response.status_code == 200
    assert response.json()["rendered_job_ids"] == [first, second]
    assert response.json()["download_url"].startswith("/exports/")
    assert len(run_engine.call_args_list[2].args[1]["sources"]) == 2
    assert run_engine.call_args_list[0].args[1]["max_total_duration_seconds"] == 8
    assert run_engine.call_args_list[1].args[1]["max_total_duration_seconds"] == 3
    assert sum(
        cut["end_seconds"] - cut["start_seconds"]
        for source in run_engine.call_args_list[2].args[1]["sources"]
        for cut in source["cuts"]
    ) == 8


def test_combined_export_download_route_only_serves_exports():
    output = DATA_DIR / "exports" / "combined.mp4"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(b"video-bytes")
    response = client.get("/exports/combined.mp4")
    assert response.status_code == 200
    assert response.content == b"video-bytes"


def test_duration_cap_is_deterministic_and_does_not_change_blur_selection():
    analysis = {
        "analysis": {"duration_seconds": 20},
        "cuts": [{"start_seconds": 2, "end_seconds": 12, "score": 0, "reason": "clear"}],
    }

    eight_second_plan = validate_edit_plan(analysis, minimum=1, maximum=8)
    four_second_plan = validate_edit_plan(analysis, minimum=1, maximum=4)

    # Both limits are derived from the same 2–12 second clear shot. Only the
    # deterministic final duration differs; no random re-selection occurs.
    assert eight_second_plan["cuts"] == [{"start_seconds": 3, "end_seconds": 11, "score": 0, "reason": "clear"}]
    assert four_second_plan["cuts"] == [{"start_seconds": 5, "end_seconds": 9, "score": 0, "reason": "clear"}]


def test_duration_cap_is_shared_by_multiple_selected_cuts():
    analysis = {
        "analysis": {"duration_seconds": 20},
        "cuts": [
            {"start_seconds": 0, "end_seconds": 3, "score": 0, "reason": "clear"},
            {"start_seconds": 5, "end_seconds": 10, "score": 0, "reason": "clear"},
        ],
    }

    plan = validate_edit_plan(analysis, minimum=1, maximum=4)

    assert plan["cuts"] == [
        {"start_seconds": 0, "end_seconds": 3, "score": 0, "reason": "clear"},
        {"start_seconds": 7, "end_seconds": 8, "score": 0, "reason": "clear"},
    ]
