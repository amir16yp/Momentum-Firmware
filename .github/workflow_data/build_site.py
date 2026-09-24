#!/usr/bin/env python3
"""Render the README and bundle the qFlipper package for website deployment."""

import argparse
from html import escape
from pathlib import Path
import shutil

import markdown


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--readme", type=Path, required=True)
    parser.add_argument("--dist", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()

    packages = sorted(args.dist.glob("f7-*/flipper-z-f7-update-*.tgz"))
    if len(packages) != 1:
        parser.error(f"Expected exactly one f7 update package, found {len(packages)}")
    package = packages[0]
    if package.stat().st_size == 0:
        parser.error(f"Update package is empty: {package}")

    content = markdown.markdown(
        args.readme.read_text(encoding="utf-8-sig"),
        extensions=["extra", "toc"],
        output_format="html",
    )
    downloads = args.output / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    download_name = "rectum-firmware-f7-update.tgz"
    shutil.copyfile(package, downloads / download_name)
    page = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Rectum Firmware</title>
  <style>
    :root {{ color-scheme: light dark; }}
    body {{ max-width: 52rem; margin: 3rem auto; padding: 0 1.25rem;
           font: 1rem/1.65 system-ui, sans-serif; }}
    h1, h2, h3 {{ line-height: 1.25; }}
    h3 {{ margin-top: 2rem; }}
    a {{ color: light-dark(#0758b0, #8ac4ff); }}
    .download {{ display: inline-block; padding: .8rem 1.2rem; border-radius: .5rem;
                 background: #0758b0; color: white; font-weight: 700; text-decoration: none; }}
    a:focus-visible {{ outline: 3px solid currentColor; outline-offset: 4px; }}
    .build {{ overflow-wrap: anywhere; font-size: .9rem; }}
    pre {{ overflow-x: auto; }}
    img {{ max-width: 100%; }}
  </style>
</head>
<body>
  <main>
    <section aria-label="Firmware download">
      <a class="download" href="downloads/{download_name}" download>Download Rectum Firmware</a>
      <p>Flipper Zero (f7) update package. Install the downloaded .tgz file using
         qFlipper's <strong>Install from file</strong> option.</p>
      <p class="build">Build commit: <code>{escape(args.commit)}</code></p>
    </section>
    <article>
{content}
    </article>
  </main>
</body>
</html>
"""
    (args.output / "index.html").write_text(page, encoding="utf-8")


if __name__ == "__main__":
    main()
