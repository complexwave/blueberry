# Blueberry Documentation — Sphinx configuration

project = "Blueberry"
author = "Blueberry Contributors"
copyright = "2026, Blueberry Contributors"

extensions = [
    "myst_parser",
]

# MyST settings
myst_enable_extensions = [
    "colon_fence",      # ::: directive syntax
    "deflist",          # definition lists
    "fieldlist",        # field lists
    "tasklist",         # - [x] checkboxes
]
myst_heading_anchors = 3  # auto-generate anchors for h1-h3

# Source settings
source_suffix = {
    ".rst": "restructuredtext",
    ".md": "markdown",
}

# Theme
html_theme = "sphinx_rtd_theme"
html_theme_options = {
    "navigation_depth": 3,
    "collapse_navigation": False,
}

# Custom CSS
html_static_path = ["_static"]
html_css_files = ["custom.css"]

# Output
exclude_patterns = ["_build"]
