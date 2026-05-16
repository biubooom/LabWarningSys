from __future__ import annotations

from pathlib import Path
from typing import Any, Dict, Sequence

try:
    from fastapi import FastAPI, HTTPException
    from pydantic import BaseModel
except ImportError:
    FastAPI = None
    HTTPException = None
    BaseModel = object

try:
    from .sequence_classifier import DEFAULT_ARTIFACT_DIR, load_sequence_bundle, predict_sequence
except ImportError:
    from sequence_classifier import DEFAULT_ARTIFACT_DIR, load_sequence_bundle, predict_sequence


class PredictRequest(BaseModel):
    sequence: list[list[list[float]]]


def _require_fastapi() -> None:
    if FastAPI is None or HTTPException is None or BaseModel is object:
        raise RuntimeError("FastAPI support requires the 'fastapi' package.")


def predict_payload(payload: Dict[str, Any], artifact_dir: str | Path = DEFAULT_ARTIFACT_DIR) -> Dict[str, Any]:
    if "sequence" not in payload:
        raise ValueError("payload must contain 'sequence'")
    bundle = load_sequence_bundle(artifact_dir)
    result = predict_sequence(bundle, payload["sequence"])
    return {
        "state_label": result.state_label,
        "display_name": result.display_name,
        "confidence": result.confidence,
        "probabilities": result.probabilities,
        "origin_label": result.origin_label,
        "origin_display_name": result.origin_display_name,
        "origin_confidence": result.origin_confidence,
        "origin_probabilities": result.origin_probabilities,
    }


def create_app(artifact_dir: str | Path = DEFAULT_ARTIFACT_DIR) -> FastAPI:
    _require_fastapi()
    resolved_artifact_dir = Path(artifact_dir)
    app = FastAPI(title="LabWarningSys GRU Backend", version="1.0.0")

    @app.get("/health")
    def health() -> Dict[str, str]:
        return {"status": "ok"}

    @app.post("/predict")
    def predict_endpoint(request: PredictRequest) -> Dict[str, Any]:
        try:
            return predict_payload({"sequence": request.sequence}, artifact_dir=resolved_artifact_dir)
        except FileNotFoundError as exc:
            raise HTTPException(status_code=503, detail=f"model artifacts not found: {exc}") from exc
        except ValueError as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc
        except RuntimeError as exc:
            raise HTTPException(status_code=500, detail=str(exc)) from exc

    return app


app = create_app() if FastAPI is not None else None
