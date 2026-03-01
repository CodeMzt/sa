import pdfplumber

# 读取PDF文件
with pdfplumber.open('reference/EL05.pdf') as pdf:
    # 打印PDF基本信息
    print(f"PDF页数: {len(pdf.pages)}")
    
    # 读取第一页内容
    first_page = pdf.pages[0]
    text = first_page.extract_text()
    print("\n第一页内容:")
    print(text[:1000])  # 只打印前1000个字符
    
    # 读取所有页内容
    all_text = []
    for page in pdf.pages:
        page_text = page.extract_text()
        if page_text:
            all_text.append(page_text)
    
    print(f"\n总字符数: {len(''.join(all_text))}")
    print("\nPDF读取成功！")
