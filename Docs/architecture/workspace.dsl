workspace "D-Engine" "High-level architecture workspace for D-Engine" {

    model {
        developer = person "Engine Developer" "Reads, builds, extends, and validates D-Engine."

        dengine = softwareSystem "D-Engine" "Header-first C++ engine focused on contracts-first APIs and deterministic, auditable behavior." {
            core = container "Source/Core" "Contracts, foundations, null backends, and interop helpers." "C++"
            modules = container "Source/Modules" "Optional modules and example backends." "C++"
            tests = container "tests" "Smoke tests, build-only checks, ABI checks, and self-containment tests." "C++"
            tools = container "tools" "Policy, packaging, benchmarking, and documentation tooling." "PowerShell and Python"
            bench = container "bench" "Bench baselines and performance comparison data." "JSON and Markdown"
            external = container "External/Rust/NullWindowModule" "Historical Rust interop example around the ABI boundary." "Rust"
        }

        developer -> dengine "Reads, builds, and extends"
        modules -> core "Builds on"
        tests -> core "Validates"
        tests -> modules "Validates"
        tools -> core "Checks and packages"
        tools -> bench "Maintains"
        bench -> core "Captures baselines for"
        external -> core "Exercises ABI concepts against"
    }

    views {
        systemLandscape "landscape" "Project-wide orientation." {
            include *
            autoLayout lr
        }

        container dengine "containers" "Repository and runtime boundaries." {
            include *
            autoLayout lr
        }

        theme default
    }
}
