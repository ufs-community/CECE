import yaml
import matplotlib.pyplot as plt
import sys
import os


def visualize_optimized_stack(config_path):
    """
    Visualizes the optimized stacking plan, highlighting the fused kernel structure.
    """
    if not os.path.exists(config_path):
        print(f"Error: Config file {config_path} not found.")
        return

    with open(config_path, "r") as f:
        config = yaml.safe_load(f)

    species_dict = config.get("species", {})
    if not species_dict:
        print("No species found in configuration.")
        return

    for species, layers in species_dict.items():
        # Mimic StackingEngine's PreCompile sorting
        sorted_layers = sorted(layers, key=lambda x: x.get("hierarchy", 0))

        hierarchies = [layer.get("hierarchy", 0) for layer in sorted_layers]
        labels = [
            f"{layer.get('field', '??')}\n({layer.get('operation', 'add')})"
            for layer in sorted_layers
        ]

        plt.figure(figsize=(10, 6))
        colors = [
            "green" if layer.get("operation") == "add" else "orange"
            for layer in sorted_layers
        ]

        plt.bar(
            range(len(hierarchies)),
            hierarchies,
            color=colors,
            alpha=0.7,
            edgecolor="black",
        )

        # Add a "Fused Kernel" bracket/text
        plt.text(
            len(hierarchies) / 2 - 0.5,
            max(hierarchies) * 1.1 if hierarchies else 1,
            "FUSED KOKKOS KERNEL",
            ha="center",
            fontweight="bold",
            color="red",
            bbox=dict(facecolor="white", alpha=0.5, edgecolor="red"),
        )

        plt.xlabel("Layer Execution Order")
        plt.ylabel("Hierarchy Level")
        plt.title(f"Optimized Stacking Plan: {species}")
        plt.xticks(range(len(labels)), labels)
        plt.grid(axis="y", linestyle="--", alpha=0.6)

        # Legend
        from matplotlib.lines import Line2D

        custom_lines = [
            Line2D([0], [0], color="green", lw=4),
            Line2D([0], [0], color="orange", lw=4),
        ]
        plt.legend(custom_lines, ["Add", "Replace"], loc="upper left")

        output_file = f"{species}_optimized_stack.png"
        plt.savefig(output_file)
        print(f"Generated visualization: {output_file}")


if __name__ == "__main__":
    cfg = sys.argv[1] if len(sys.argv) > 1 else "cece_config.yaml.example"
    visualize_optimized_stack(cfg)
