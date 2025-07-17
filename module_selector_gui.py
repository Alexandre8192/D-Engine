import os
import json
import tkinter as tk
from tkinter import ttk, messagebox

# ============================
# Config paths
# ============================
MODULES_DIR = "Modules"
OUTPUT_HEADER = os.path.join("Config", "ModulesConfig.hpp")
CATEGORY_ORDER = {
    "CoreMinimal": 0,
    "SoftCore": 1,
    "Feature": 2
}

# ============================
# Load metadata
# ============================
def load_all_modules():
    modules = []

    for folder_name in os.listdir(MODULES_DIR):
        folder_path = os.path.join(MODULES_DIR, folder_name)
        metadata_path = os.path.join(folder_path, "ModuleMetadata.json")

        if not os.path.isdir(folder_path) or not os.path.isfile(metadata_path):
            continue

        with open(metadata_path, "r") as f:
            try:
                data = json.load(f)
                module = {
                    "ID": data.get("ID", folder_name),
                    "Name": data.get("Name", folder_name),
                    "Category": data.get("Category", "Feature"),
                    "DefaultEnabled": data.get("DefaultEnabled", False),
                    "Keywords": data.get("Keywords", []),
                    "Description": data.get("Description", ""),
                }
                modules.append(module)
            except json.JSONDecodeError as e:
                print(f"[ERROR] Cannot parse {metadata_path}: {e}")
    
    # Sort by category then by name
    modules.sort(key=lambda m: (CATEGORY_ORDER.get(m["Category"], 99), m["Name"]))
    return modules

# ============================
# Generate ModulesConfig.hpp
# ============================
def generate_config(modules, check_vars):
    os.makedirs(os.path.dirname(OUTPUT_HEADER), exist_ok=True)

    with open(OUTPUT_HEADER, "w") as out:
        out.write("// =========================================\n")
        out.write("// AUTO-GENERATED FILE — DO NOT MODIFY\n")
        out.write("// Run module_selector_gui.py to regenerate\n")
        out.write("// =========================================\n\n")
        out.write("#pragma once\n\n")

        current_category = None
        for module in modules:
            cat = module["Category"]
            if cat != current_category:
                current_category = cat
                out.write(f"// ------------------------------------------\n")
                out.write(f"// {cat} Modules\n")
                out.write(f"// ------------------------------------------\n")

            macro = f"DNG_MODULE_{module['ID'].upper()}"
            value = "1" if check_vars[module['ID']].get() else "0"
            out.write(f"#define {macro:<30} {value}   // {module['Name']}\n")

    messagebox.showinfo("Success", "ModulesConfig.hpp has been generated successfully!")

# ============================
# Tkinter UI
# ============================
def launch_gui():
    root = tk.Tk()
    root.title("D-Engine Module Selector")

    modules = load_all_modules()
    check_vars = {}

    # Scrollable frame
    canvas = tk.Canvas(root, height=500)
    scrollbar = ttk.Scrollbar(root, orient="vertical", command=canvas.yview)
    scrollable_frame = ttk.Frame(canvas)

    scrollable_frame.bind(
        "<Configure>",
        lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
    )

    canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")
    canvas.configure(yscrollcommand=scrollbar.set)

    # Display module checkboxes
    current_category = None
    for module in modules:
        if module["Category"] != current_category:
            label = ttk.Label(scrollable_frame, text=f"{module['Category']} Modules", font=("Segoe UI", 12, "bold"))
            label.pack(anchor="w", pady=(10, 0))
            current_category = module["Category"]

        var = tk.BooleanVar(value=module["DefaultEnabled"])
        check_vars[module["ID"]] = var

        frame = ttk.Frame(scrollable_frame)
        frame.pack(fill="x", padx=10, pady=2)

        cb = ttk.Checkbutton(frame, text=module["Name"], variable=var)
        cb.pack(side="left")

        if module["Description"]:
            desc = ttk.Label(frame, text=f"— {module['Description']}", font=("Segoe UI", 9), foreground="#555555")
            desc.pack(side="left")

    # Generate button
    def on_generate():
        generate_config(modules, check_vars)

    generate_button = ttk.Button(root, text="Generate ModulesConfig.hpp", command=on_generate)
    generate_button.pack(pady=10)

    canvas.pack(side="left", fill="both", expand=True)
    scrollbar.pack(side="right", fill="y")

    root.mainloop()

# ============================
# Entry point
# ============================
if __name__ == "__main__":
    launch_gui()
