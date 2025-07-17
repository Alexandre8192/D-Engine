import os
import json

# ====================================================================
# Configuration
# ====================================================================

# The folder where all your modules are located
MODULES_FOLDER = "Modules"

# The output path of the generated ModulesConfig.hpp
OUTPUT_HEADER = os.path.join("Config", "ModulesConfig.hpp")

# Optional: sort order for categories (SoftCore before Feature, etc.)
CATEGORY_ORDER = {
    "CoreMinimal": 0,
    "SoftCore": 1,
    "Feature": 2
}


# ====================================================================
# Helper: Parse each module's metadata
# ====================================================================
def load_module_metadata(module_path):
    """Load the ModuleMetadata.json file and return a dict with info."""
    metadata_path = os.path.join(module_path, "ModuleMetadata.json")
    if not os.path.isfile(metadata_path):
        return None  # No metadata file found

    with open(metadata_path, "r") as file:
        try:
            data = json.load(file)
            return data
        except json.JSONDecodeError as e:
            print(f"[ERROR] Failed to parse {metadata_path}: {e}")
            return None


# ====================================================================
# Main Logic
# ====================================================================
def main():
    modules = []

    # Iterate over each subfolder inside the Modules directory
    for folder_name in os.listdir(MODULES_FOLDER):
        module_path = os.path.join(MODULES_FOLDER, folder_name)
        if not os.path.isdir(module_path):
            continue  # Skip non-directories

        metadata = load_module_metadata(module_path)
        if metadata is None:
            continue  # Skip if failed to parse

        # Extract required fields from metadata
        module_info = {
            "ID": metadata.get("ID", folder_name),
            "Category": metadata.get("Category", "Feature"),
            "DefaultEnabled": metadata.get("DefaultEnabled", False),
            "Name": metadata.get("Name", folder_name)
        }

        modules.append(module_info)

    # Sort modules by category (optional, for nicer organization)
    modules.sort(key=lambda m: CATEGORY_ORDER.get(m["Category"], 99))

    # Generate the C++ header
    os.makedirs(os.path.dirname(OUTPUT_HEADER), exist_ok=True)
    with open(OUTPUT_HEADER, "w") as header:

        # Write header guard and comments
        header.write("// ====================================================\n")
        header.write("// AUTO-GENERATED FILE — DO NOT MODIFY MANUALLY\n")
        header.write("// Run generate_modules_config.py to regenerate this file\n")
        header.write("// ====================================================\n\n")
        header.write("#pragma once\n\n")

        current_category = None

        for module in modules:
            # Group by category (SoftCore, Feature, etc.)
            if module["Category"] != current_category:
                current_category = module["Category"]
                header.write(f"// ------------------------------------------\n")
                header.write(f"// {current_category} Modules\n")
                header.write(f"// ------------------------------------------\n")

            # Write define line
            macro = f"DNG_MODULE_{module['ID'].upper()}"
            value = "1" if module["DefaultEnabled"] else "0"
            header.write(f"#define {macro:<30} {value}   // {module['Name']}\n")

        print(f"[SUCCESS] ModulesConfig.hpp has been generated at: {OUTPUT_HEADER}")


# ====================================================================
# Entry point
# ====================================================================
if __name__ == "__main__":
    main()
