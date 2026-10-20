"""MCP adapter for the same REST tools exposed by the FastAPI service."""
import os
from pathlib import Path
from typing import Literal

import httpx
from mcp.server.fastmcp import FastMCP


API_BASE_URL = os.getenv("API_BASE_URL", "http://localhost:8000").rstrip("/")
mcp = FastMCP("Automated Video Editor")


@mcp.tool()
def get_job_status(job_id: str) -> dict:
    """Read an editor job's analysis, cuts, status, or export location."""
    response = httpx.get(f"{API_BASE_URL}/jobs/{job_id}", timeout=30)
    response.raise_for_status()
    return response.json()


@mcp.tool()
def analyze_video(
    job_id: str,
    min_clip_seconds: float = 4,
    max_total_duration_seconds: float = 8,
    blur_threshold: float = 5,
) -> dict:
    """Analyze a job. Lower blur_threshold allows more blur; increase max_total_duration_seconds for a longer edit."""
    response = httpx.post(f"{API_BASE_URL}/jobs/{job_id}/analyze", json={
        "min_clip_seconds": min_clip_seconds,
        "max_total_duration_seconds": max_total_duration_seconds,
        "blur_threshold": blur_threshold,
    }, timeout=190)
    response.raise_for_status()
    return response.json()


@mcp.tool()
def adjust_edit(
    job_id: str,
    blur_tolerance: Literal["more", "default", "less"] = "default",
    clip_length: Literal["shorter", "default", "longer"] = "default",
) -> dict:
    """Re-cut a job from a plain-language preference.

    Use blur_tolerance="more" for requests such as "allow a bit more blur";
    use "less" for a stricter edit. Use clip_length="longer" or "shorter"
    when the user asks to change the automatic edit duration.
    """
    thresholds = {"more": 2, "default": 5, "less": 15}
    lengths = {"shorter": 6, "default": 8, "longer": 10}
    return analyze_video(
        job_id,
        max_total_duration_seconds=lengths[clip_length],
        blur_threshold=thresholds[blur_tolerance],
    )


@mcp.tool()
def compile_videos(
    job_ids: list[str],
    min_clip_seconds: float = 1,
    max_total_duration_seconds: float = 8,
    blur_threshold: float = 5,
    output_name: str = "edited-video.mp4",
) -> dict:
    """Automatically analyze, trim, and concatenate already-uploaded clips in job_ids order.

    Use this after separate uploads. Every clip is reanalyzed before rendering,
    so the output contains the C++-selected trim of each qualifying clip.
    """
    response = httpx.post(f"{API_BASE_URL}/compilations", json={
        "job_ids": job_ids,
        "min_clip_seconds": min_clip_seconds,
        "max_total_duration_seconds": max_total_duration_seconds,
        "blur_threshold": blur_threshold,
        "output_name": output_name,
    }, timeout=300)
    response.raise_for_status()
    return response.json()


@mcp.tool()
def edit_videos(
    local_paths: list[str],
    min_clip_seconds: float = 1,
    max_total_duration_seconds: float = 8,
    blur_threshold: float = 5,
    output_name: str = "edited-video.mp4",
) -> dict:
    """Upload one or more local clips, automatically trim each, and join them into one MP4.

    Use this for a request such as "edit these clips for me". Lower
    blur_threshold allows more blur; the default 5 is intentionally tolerant.
    """
    if not local_paths:
        raise ValueError("Provide at least one local video path")
    uploads = [upload_video(path) for path in local_paths]
    compiled = compile_videos(
        [upload["id"] for upload in uploads],
        min_clip_seconds=min_clip_seconds,
        max_total_duration_seconds=max_total_duration_seconds,
        blur_threshold=blur_threshold,
        output_name=output_name,
    )
    return {"uploads": uploads, "compilation": compiled}


@mcp.tool()
def render_video(job_id: str, output_name: str = "edited.mp4") -> dict:
    """Render the approved cut list to an MP4 export."""
    response = httpx.post(f"{API_BASE_URL}/jobs/{job_id}/export", json={"output_name": output_name}, timeout=190)
    response.raise_for_status()
    return response.json()


@mcp.tool()
def upload_video(local_path: str) -> dict:
    """Upload a local video file to the REST service and return its job ID."""
    path = Path(local_path).expanduser().resolve()
    if not path.is_file():
        raise ValueError(f"Video does not exist: {path}")
    with path.open("rb") as stream:
        response = httpx.post(f"{API_BASE_URL}/jobs", files={"file": (path.name, stream, "video/mp4")}, timeout=190)
    response.raise_for_status()
    return response.json()


if __name__ == "__main__":
    mcp.run()
