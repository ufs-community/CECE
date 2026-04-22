import yaml
import matplotlib.pyplot as plt


def visualize_stacking_plan(config_path):
    """
    Simulates and visualizes the emission stacking plan defined in the CECE config.
    This serves as a Python-side orchestration and visualization tool for the
    high-performance C++ stacking engine.
    """
    with open(config_path, "r") as f:
        config = yaml.safe_load(f)

    if "species" not in config:
        print("No species found in config.")
        return

    for species, layers in config["species"].items():
        print(f"--- Stacking Plan for {species} ---")

        # Sort by hierarchy (mimicking StackingEngine logic)
        sorted_layers = sorted(layers, key=lambda x: x.get("hierarchy", 0))

        stack_visual = []
        for i, layer in enumerate(sorted_layers):
            op = layer.get("operation", "add")
            hier = layer.get("hierarchy", 0)
            field = layer.get("field", "unknown")
            print(f"Layer {i}: {op.upper()} {field} (Hierarchy: {hier})")
            stack_visual.append(hier)

        # Simple plot of hierarchy levels
        plt.figure(figsize=(8, 4))
        plt.bar(range(len(stack_visual)), stack_visual, color="skyblue")
        plt.xlabel("Layer Index (Sorted)")
        plt.ylabel("Hierarchy Level")
        plt.title(f"Emission Stacking Hierarchy: {species}")
        plt.xticks(range(len(stack_visual)))
        plt.grid(axis="y", linestyle="--", alpha=0.7)

        save_path = f"{species}_stacking_plan.png"
        plt.savefig(save_path)
        print(f"Saved stacking plan visualization to {save_path}")


if __name__ == "__main__":
    import sys

    config_file = sys.argv[1] if len(sys.argv) > 1 else "cece_config.yaml.example"
    visualize_stacking_plan(config_file)
