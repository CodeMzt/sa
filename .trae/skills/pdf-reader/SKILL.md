---
name: "pdf-reader"
description: "Reads PDF files and extracts text content. Invoke when user needs to read or analyze PDF documents."
---

# PDF Reader Skill

This skill provides functionality to read PDF files and extract text content using the pdfplumber library.

## How to Use

1. **Ensure pdfplumber is installed**: `pip install pdfplumber`
2. **Create a Python script** to read PDF files:

```python
import pdfplumber

# Read PDF file
with pdfplumber.open('path/to/pdf/file.pdf') as pdf:
    # Get number of pages
    num_pages = len(pdf.pages)
    print(f"PDF页数: {num_pages}")
    
    # Extract text from first page
    first_page = pdf.pages[0]
    text = first_page.extract_text()
    print("\n第一页内容:")
    print(text[:1000])  # Print first 1000 characters
    
    # Extract text from all pages
    all_text = []
    for page in pdf.pages:
        page_text = page.extract_text()
        if page_text:
            all_text.append(page_text)
    
    total_chars = len(''.join(all_text))
    print(f"\n总字符数: {total_chars}")
    print("\nPDF读取成功！")
```

## Example Usage

To read a PDF file named `EL05.pdf` in the `reference` directory:

```python
import pdfplumber

with pdfplumber.open('reference/EL05.pdf') as pdf:
    print(f"PDF页数: {len(pdf.pages)}")
    
    # Extract and print text from all pages
    for i, page in enumerate(pdf.pages):
        text = page.extract_text()
        if text:
            print(f"\n第{i+1}页内容:")
            print(text[:500])  # Print first 500 characters of each page
```

## Features

- Extract text from PDF files
- Get page count
- Extract text from specific pages
- Extract text from all pages

## Dependencies

- pdfplumber: `pip install pdfplumber`

## Notes

- This skill uses the pdfplumber library which is built on top of PyPDF2 and pdfminer.six
- It can handle most PDF files, including those with text embedded in images (OCR not included)
- For large PDF files, consider processing pages incrementally to avoid memory issues
