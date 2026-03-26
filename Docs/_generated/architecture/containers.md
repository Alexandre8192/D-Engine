# Containers

This page is generated from the Structurizr workspace.
It may be overwritten by tooling.

```mermaid
graph LR
  linkStyle default fill:#ffffff

  subgraph diagram ["Container View: D-Engine"]
    style diagram fill:#ffffff,stroke:#ffffff

    subgraph 2 ["D-Engine"]
      style 2 fill:#ffffff,stroke:#0b4884,color:#0b4884

      3("<div style='font-weight: bold'>Source/Core</div><div style='font-size: 70%; margin-top: 0px'>[Container: C++]</div><div style='font-size: 80%; margin-top:10px'>Contracts, foundations, null<br />backends, and interop<br />helpers.</div>")
      style 3 fill:#438dd5,stroke:#2e6295,color:#ffffff
      4("<div style='font-weight: bold'>Source/Modules</div><div style='font-size: 70%; margin-top: 0px'>[Container: C++]</div><div style='font-size: 80%; margin-top:10px'>Optional modules and example<br />backends.</div>")
      style 4 fill:#438dd5,stroke:#2e6295,color:#ffffff
      5("<div style='font-weight: bold'>tests</div><div style='font-size: 70%; margin-top: 0px'>[Container: C++]</div><div style='font-size: 80%; margin-top:10px'>Smoke tests, build-only<br />checks, ABI checks, and<br />self-containment tests.</div>")
      style 5 fill:#438dd5,stroke:#2e6295,color:#ffffff
      6("<div style='font-weight: bold'>tools</div><div style='font-size: 70%; margin-top: 0px'>[Container: PowerShell and Python]</div><div style='font-size: 80%; margin-top:10px'>Policy, packaging,<br />benchmarking, and<br />documentation tooling.</div>")
      style 6 fill:#438dd5,stroke:#2e6295,color:#ffffff
      7("<div style='font-weight: bold'>bench</div><div style='font-size: 70%; margin-top: 0px'>[Container: JSON and Markdown]</div><div style='font-size: 80%; margin-top:10px'>Bench baselines and<br />performance comparison data.</div>")
      style 7 fill:#438dd5,stroke:#2e6295,color:#ffffff
      8("<div style='font-weight: bold'>External/Rust/NullWindowModule</div><div style='font-size: 70%; margin-top: 0px'>[Container: Rust]</div><div style='font-size: 80%; margin-top:10px'>Historical Rust interop<br />example around the ABI<br />boundary.</div>")
      style 8 fill:#438dd5,stroke:#2e6295,color:#ffffff
    end

    4-. "<div>Builds on</div><div style='font-size: 70%'></div>" .->3
    5-. "<div>Validates</div><div style='font-size: 70%'></div>" .->3
    5-. "<div>Validates</div><div style='font-size: 70%'></div>" .->4
    6-. "<div>Checks and packages</div><div style='font-size: 70%'></div>" .->3
    6-. "<div>Maintains</div><div style='font-size: 70%'></div>" .->7
    7-. "<div>Captures baselines for</div><div style='font-size: 70%'></div>" .->3
    8-. "<div>Exercises ABI concepts<br />against</div><div style='font-size: 70%'></div>" .->3

  end
```

