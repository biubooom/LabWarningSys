from __future__ import annotations

from pathlib import Path
from typing import Iterable

try:
    from .model_types import FEATURE_NAMES, StaticStateModel
except ImportError:
    from model_types import FEATURE_NAMES, StaticStateModel


HEADER_GUARD = "GENERATED_STATIC_STATE_MODEL_H"


def _format_float_array(values: Iterable[float]) -> str:
    return ", ".join(f"{value:.6f}f" for value in values)


def build_c_header(model: StaticStateModel) -> str:
    center_rows = ",\n    ".join(
        "{" + _format_float_array(center) + "}" for center in model.centers
    )
    label_rows = ",\n    ".join(f"\"{model.cluster_label_map[idx]}\"" for idx in range(len(model.centers)))
    feature_name_rows = ", ".join(f"\"{name}\"" for name in FEATURE_NAMES)

    return f"""#ifndef {HEADER_GUARD}
#define {HEADER_GUARD}

#ifdef __cplusplus
extern "C" {{
#endif

#define STATIC_STATE_CLUSTER_COUNT {len(model.centers)}
#define STATIC_STATE_FEATURE_COUNT {len(FEATURE_NAMES)}

static const float g_static_state_centers[STATIC_STATE_CLUSTER_COUNT][STATIC_STATE_FEATURE_COUNT] = {{
    {center_rows}
}};

static const float g_static_state_feature_min[STATIC_STATE_FEATURE_COUNT] = {{
    {_format_float_array(model.feature_min)}
}};

static const float g_static_state_feature_max[STATIC_STATE_FEATURE_COUNT] = {{
    {_format_float_array(model.feature_max)}
}};

static const char * const g_static_state_cluster_labels[STATIC_STATE_CLUSTER_COUNT] = {{
    {label_rows}
}};

static const char * const g_static_state_feature_names[STATIC_STATE_FEATURE_COUNT] = {{
    {feature_name_rows}
}};

#ifdef __cplusplus
}}
#endif

#endif /* {HEADER_GUARD} */
"""


def export_c_header(model: StaticStateModel, path: str | Path) -> Path:
    output_path = Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(build_c_header(model), encoding="ascii")
    return output_path
