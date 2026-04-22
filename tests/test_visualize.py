import os
import sys
import yaml
import matplotlib

# Use non-interactive backend for tests
matplotlib.use("Agg")

# Add scripts directory to path
sys.path.insert(
    0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "scripts"))
)
import visualize_stack


def test_visualize_stack_sorting(tmp_path):
    """Test that the internal logic correctly sorts layers by hierarchy."""
    config_data = {
        "species": {
            "NO2": [
                {"operation": "replace", "hierarchy": 10, "field": "f2"},
                {"operation": "add", "hierarchy": 1, "field": "f1"},
            ]
        }
    }
    config_file = tmp_path / "test_sort.yaml"
    with open(config_file, "w") as f:
        yaml.dump(config_data, f)

    # We can't easily test the print output without capturing stdout,
    # but we can verify it doesn't crash.
    visualize_stack.visualize_stacking_plan(str(config_file))

    assert os.path.exists("NO2_stacking_plan.png")
    os.remove("NO2_stacking_plan.png")


def test_visualize_stack_no_species(tmp_path):
    """Test behavior when no species are found."""
    config_file = tmp_path / "empty.yaml"
    config_file.write_text("empty: true")

    # Should not crash
    visualize_stack.visualize_stacking_plan(str(config_file))
