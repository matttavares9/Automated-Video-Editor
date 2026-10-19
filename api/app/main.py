import shutil
import uuid
from pathlib import Path

from fastapi import Depends, FastAPI, File, HTTPException, UploadFile
from fastapi.responses import FileResponse
from sqlalchemy.orm import Session

from .db import EditDecision, Job, SessionLocal, initialize_database
from .engine import DATA_DIR, EngineError, run_engine
from .models import AnalyzeRequest, CompilationResponse, CompileRequest, Cut, ExportRequest, JobResponse

app = FastAPI(title="FastAPI Blur Trimmer API", version="0.1.0")


@app.on_event("startup")
def startup() -> None:
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    initialize_database()


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


def serialize(job: Job) -> JobResponse:
    return JobResponse(
        id=job.id, status=job.status, source_path=job.source_path,
        analysis=job.analysis, export_path=job.export_path, error=job.error,
        cuts=[Cut(start_seconds=x.start_seconds, end_seconds=x.end_seconds, score=x.score, reason=x.reason) for x in job.decisions],
    )


def fetch_job(job_id: str, db: Session) -> Job:
    job = db.get(Job, job_id)
    if not job:
        raise HTTPException(404, "Job not found")
    return job


@app.get("/health")
def health():
    return {"status": "ok"}


@app.post("/jobs", response_model=JobResponse, status_code=201)
async def upload_video(file: UploadFile = File(...), db: Session = Depends(get_db)):
    suffix = Path(file.filename or "upload.mp4").suffix.lower() or ".mp4"
    if suffix not in {".mp4", ".mov", ".avi", ".mkv", ".webm"}:
        raise HTTPException(415, "Upload a supported video file")
    job_id = str(uuid.uuid4())
    destination = DATA_DIR / "uploads" / f"{job_id}{suffix}"
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("wb") as output:
        shutil.copyfileobj(file.file, output)
    job = Job(id=job_id, source_path=str(destination), status="uploaded")
    db.add(job); db.commit(); db.refresh(job)
    return serialize(job)


@app.post("/jobs/{job_id}/analyze", response_model=JobResponse)
def analyze_job(job_id: str, request: AnalyzeRequest, db: Session = Depends(get_db)):
    job = fetch_job(job_id, db)
    try:
        analyze_and_save(job, request)
        db.commit(); db.refresh(job)
        return serialize(job)
    except (EngineError, OSError, ValueError) as error:
        job.status, job.error = "failed", str(error); db.commit()
        raise HTTPException(422, str(error)) from error


@app.post("/compilations", response_model=CompilationResponse)
def compile_jobs(request: CompileRequest, db: Session = Depends(get_db)):
    jobs = [fetch_job(job_id, db) for job_id in request.job_ids]
    sources, rendered_ids, skipped_ids = [], [], []
    remaining_duration = request.max_total_duration_seconds
    try:
        for job in jobs:
            # The maximum is for the finished compilation, not for every
            # uploaded file. Pass the unspent portion to the deterministic
            # post-selection cap so later uploads cannot exceed the total.
            if remaining_duration + 1e-6 < request.min_clip_seconds:
                skipped_ids.append(job.id)
                continue
            analysis_request = AnalyzeRequest(
                min_clip_seconds=request.min_clip_seconds,
                max_total_duration_seconds=remaining_duration,
                blur_threshold=request.blur_threshold,
            )
            analyze_and_save(job, analysis_request)
            cuts = [Cut(start_seconds=x.start_seconds, end_seconds=x.end_seconds, score=x.score, reason=x.reason) for x in job.decisions]
            if cuts:
                sources.append({"input_path": job.source_path, "cuts": [cut.model_dump() for cut in cuts]})
                rendered_ids.append(job.id)
                remaining_duration -= sum(cut.end_seconds - cut.start_seconds for cut in cuts)
            else:
                skipped_ids.append(job.id)
        if not sources:
            raise ValueError("No uploaded clips contained a qualifying clear section")
        output = DATA_DIR / "exports" / f"compilation-{uuid.uuid4()}-{request.output_name}"
        result = run_engine("render", {"sources": sources, "output_path": str(output)})
        db.commit()
        return CompilationResponse(
            output_path=result["output_path"],
            download_url=f"/exports/{output.name}",
            rendered_job_ids=rendered_ids,
            skipped_job_ids=skipped_ids, frames_written=result["frames_written"],
        )
    except (EngineError, OSError, ValueError) as error:
        db.rollback()
        raise HTTPException(422, str(error)) from error


def analyze_and_save(job: Job, request: AnalyzeRequest) -> None:
    job.status, job.error = "analyzing", None
    analysis = run_engine("analyze", {"input_path": job.source_path, **request.model_dump()})
    # The C++ core selects complete clear shots. The API only applies the
    # requested deterministic duration cap before saving that selection.
    plan = validate_edit_plan(analysis, request.min_clip_seconds, request.max_total_duration_seconds)
    job.analysis, job.status = analysis, "analyzed"
    job.decisions.clear()
    for cut in plan["cuts"]:
        job.decisions.append(EditDecision(job_id=job.id, **cut))


@app.get("/jobs/{job_id}", response_model=JobResponse)
def job_status(job_id: str, db: Session = Depends(get_db)):
    return serialize(fetch_job(job_id, db))


@app.post("/jobs/{job_id}/export", response_model=JobResponse)
def export_job(job_id: str, request: ExportRequest, db: Session = Depends(get_db)):
    job = fetch_job(job_id, db)
    cuts = request.cuts or [Cut(start_seconds=x.start_seconds, end_seconds=x.end_seconds, score=x.score, reason=x.reason) for x in job.decisions]
    if not cuts:
        raise HTTPException(409, "Analyze the job or provide cuts before exporting")
    output = DATA_DIR / "exports" / f"{job.id}-{request.output_name}"
    try:
        run_engine("render", {"input_path": job.source_path, "output_path": str(output), "cuts": [cut.model_dump() for cut in cuts]})
        job.status, job.export_path, job.error = "exported", str(output), None
        db.commit(); db.refresh(job)
        return serialize(job)
    except (EngineError, OSError, ValueError) as error:
        job.status, job.error = "failed", str(error); db.commit()
        raise HTTPException(422, str(error)) from error


@app.get("/jobs/{job_id}/export")
def download_export(job_id: str, db: Session = Depends(get_db)):
    job = fetch_job(job_id, db)
    if not job.export_path or not Path(job.export_path).is_file():
        raise HTTPException(404, "Export not available")
    return FileResponse(job.export_path, media_type="video/mp4", filename=Path(job.export_path).name)


@app.get("/exports/{output_name}")
def download_compilation(output_name: str):
    """Download a combined export created by POST /compilations."""
    if Path(output_name).name != output_name:
        raise HTTPException(400, "Invalid export name")
    output = DATA_DIR / "exports" / output_name
    if not output.is_file():
        raise HTTPException(404, "Export not available")
    return FileResponse(output, media_type="video/mp4", filename=output.name)


def validate_edit_plan(analysis: dict, minimum: float, maximum: float) -> dict:
    """Cap C++ clear-shot selections deterministically without reselecting them."""
    source_duration = analysis.get("analysis", {}).get("duration_seconds")
    if source_duration is not None:
        minimum = min(minimum, source_duration)
    approved, remaining = [], maximum
    for source_cut in analysis.get("cuts", []):
        start, end = source_cut["start_seconds"], source_cut["end_seconds"]
        duration = end - start
        if duration + 1e-6 < minimum or remaining + 1e-6 < minimum:
            continue
        kept_duration = min(duration, remaining)
        # The clear shot is selected solely from blur boundaries. When a
        # duration cap is necessary, retain an exact centered portion of that
        # already-selected shot. This replaces Clip::Create's random trim.
        if kept_duration + 1e-6 < duration:
            trim_each_side = (duration - kept_duration) / 2
            start, end = start + trim_each_side, end - trim_each_side
        approved.append({
            **source_cut,
            "start_seconds": start,
            "end_seconds": end,
        })
        remaining -= kept_duration
    return {"cuts": approved}
