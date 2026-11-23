#!/usr/bin/env python3


import os
import subprocess
import sys
import argparse
import glob
import re
from typing import Match

PRINT_READ = "\033[91m"
PRINT_RESET = "\033[0m"

def _cjk_display_width(text: str) -> int:
    """Return reST title underline width (wide/fullwidth chars count as 2)."""
    import unicodedata
    width = 0
    for ch in text:
        if unicodedata.east_asian_width(ch) in ('W', 'F'):
            width += 2
        else:
            width += 1
    return width + 4

def _text_display_width(text: str) -> int:
    """Grid table 单元格对齐宽度（CJK 等宽字符计 2）。"""
    import unicodedata
    width = 0
    for ch in text:
        if unicodedata.east_asian_width(ch) in ('W', 'F'):
            width += 2
        else:
            width += 1
    return width

class MarkdownToRST:
    """Markdown → RST 转换器"""

    def __init__(self, src_path=None, dst_path=None):
        """初始化转换器"""
        self.src_path = src_path  # 源文件路径
        self.dst_path = dst_path  # 目标文件路径

    def convert_file(self, src_file, dst_file):
        """将指定的Markdown文件转换为RST文件"""
        # 构建完整的文件路径
        src_file_path = os.path.join(self.src_path, src_file) if self.src_path else src_file
        dst_file_path = os.path.join(self.dst_path, dst_file) if self.dst_path else dst_file

        # 确保目标目录存在
        if self.dst_path:
            os.makedirs(self.dst_path, exist_ok=True)

        # 读取源文件
        with open(src_file_path, "r", encoding="utf-8") as f:
            md_text = f.read()

        # 转换内容
        rst_text = self.convert(md_text)

        # 写入目标文件
        with open(dst_file_path, "w", encoding="utf-8") as f:
            f.write(rst_text)

    def convert(self, text: str) -> str:
        """将 Markdown 文本转换为 reStructuredText (RST)"""
        # 首先清理文本，移除可能导致问题的内容
        text = self._clean_text_before_conversion(text)

        # 然后在完整文本上处理代码块（因为代码块可能跨多行）
        text = self._convert_codeblocks(text)
        text = self._convert_markdown_tables(text)

        # 然后按行处理其他格式
        lines = text.splitlines()
        processed_lines = []

        for line in lines:
            if self._is_grid_table_line(line):
                processed_lines.append(line)
                continue
            # 检查是否已经包含reST语法
            if self._contains_rst_syntax(line):
                # 如果已经包含reST语法，则不进行转换，直接添加到结果中
                processed_lines.append(line)
            else:
                # 否则进行正常的Markdown到reST转换
                processed_line = line
                # 处理行内代码
                processed_line = self._convert_inline_code(processed_line)
                # 然后处理其他格式
                processed_line = self._convert_headings(processed_line)
                processed_line = self._convert_images(processed_line)
                processed_line = self._convert_link_to_translation(processed_line)
                processed_line = self._convert_bold(processed_line)
                processed_line = self._convert_italic(processed_line)
                # 链接须在 italic 之后，避免 `text <url>`_ 尾部下划线被当成强调
                processed_line = self._convert_links(processed_line)
                processed_line = self._convert_unordered_list(processed_line)
                processed_line = self._convert_ordered_list(processed_line)
                # 最后处理语言链接转换，避免被行内代码转换影响
                processed_lines.append(processed_line)

        # 将处理后的行重新组合为文本
        return '\n'.join(processed_lines)

    def _convert_link_to_translation(self, text: str) -> str:
        # 确保使用正确的反引号格式，避免出现双反引号
        # 匹配无序列表中的语言链接
        text = re.sub(r'^\s*[*-]\s+\[English\]\(\.\/README\.md\)', r':link_to_translation:`en:[English]`', text, flags=re.MULTILINE)
        text = re.sub(r'^\s*[*-]\s+\[中文\]\(\.\/README_CN\.md\)', r':link_to_translation:`zh_CN:[中文]`', text, flags=re.MULTILINE)
        # 匹配普通文本中的语言链接
        text = re.sub(r'\[English\]\(\.\/README\.md\)', r':link_to_translation:`en:[English]`', text)
        text = re.sub(r'\[中文\]\(\.\/README_CN\.md\)', r':link_to_translation:`zh_CN:[中文]`', text)
        return text


    def _contains_rst_syntax(self, line: str) -> bool:
        """检查一行文本是否已经包含reST语法"""
        # 检查常见的reST语法模式
        patterns = [
            # 检查以..开头的指令（如.. image::, .. code-block::等）
            r'^\s*\.\.',
            # 检查以:开头的指令（如:link_to_translation:）
            r'^\s*:',
            # 检查rst链接格式 `link text <url>`_
            r'`[^`]+ <[^>]+>`_',
            # 检查行内代码 ``code``
            r'``[^`]+``',
            # 检查toctree指令
            r'\.\.\s+toctree\s*::',
            # 检查note等提示框指令
            r'\.\.\s+(note|warning|tip|important|caution)\s*::'
        ]

        # 如果包含任何一个reST语法模式，则返回True
        for pattern in patterns:
            if re.search(pattern, line):
                return True

        return False

    def _convert_headings(self, text: str) -> str:
        def repl(m: Match) -> str:
            level = len(m.group(1))
            title = m.group(2).strip()
            if level == 1:
                underline = "=" * len(title) * 3
            elif level == 2:
                underline = "-" * len(title) * 3
            elif level == 3:
                underline = "," * len(title) * 3
            elif level == 4:
                underline = "." * len(title) * 3
            elif level == 5:
                underline = "*" * len(title) * 3
            else:
                underline = "~" * len(title) * 3
            return f"{title}\n{underline}\n"
        # 先处理标准的# 标题格式
        text = re.sub(r"^(#{1,6})\s+(.*)$", repl, text, flags=re.MULTILINE)
        # 再处理特殊的#. 标题格式
        text = re.sub(r"^(#)\.\s+(.*)$", repl, text, flags=re.MULTILINE)
        return text

    def _convert_images(self, text: str) -> str:
        def repl(match):
            alt_text = match.group(1)
            image_path = match.group(2)

            # 如果指定了源路径和目标路径，则调整图片相对路径
            if self.src_path and self.dst_path:
                # 判断是否为绝对路径
                if not os.path.isabs(image_path):
                    # 构建图片的绝对路径
                    absolute_image_path = os.path.join(self.src_path, image_path)
                    # 计算相对于目标路径的相对路径
                    relative_image_path = os.path.relpath(absolute_image_path, self.dst_path)
                    # 确保路径使用正斜杠（RST标准）
                    image_path = relative_image_path.replace(os.sep, '/')

            return f".. image:: {image_path}\n   :alt: {alt_text}"

        return re.sub(r'!\[(.*?)\]\((.*?)\)', repl, text)

    def _convert_links(self, text: str) -> str:
        return re.sub(r'\[(.*?)\]\((.*?)\)', r'`\1 <\2>`_', text)

    def _convert_bold(self, text: str) -> str:
        return re.sub(r'(\*\*|__)(.*?)\1', r'**\2**', text)

    def _convert_italic(self, text: str) -> str:
        """
        *italic* 或 _italic_ -> *italic*
        避免误伤普通下划线，如 link_to
        """
        # 只匹配被空格或行首行尾包围的 _xxx_
        text = re.sub(r'(?<!\w)\*(?!\*)(.+?)(?<!\*)\*(?!\w)', r'*\1*', text)
        text = re.sub(r'(?<!\w)_(?!_)(.+?)(?<!_)_(?!\w)', r'*\1*', text)
        return text

    def _convert_codeblocks(self, text: str) -> str:
        """转换Markdown代码块为reST代码块"""
        def repl(m: Match) -> str:
            lang = m.group(1).strip() if m.group(1) else ""
            code = m.group(2)
            rst = f".. code-block:: {lang}\n\n"
            for line in code.splitlines():
                # 确保代码行正确缩进，避免reST语法错误
                rst += f"   {line}\n"
            return rst + "\n"

        # 使用更健壮的正则表达式匹配代码块
        # 使用DOTALL标志使.匹配换行符，使用贪婪模式确保完整匹配代码块
        return re.sub(r"```(\w*)\n(.*?)```", repl, text, flags=re.DOTALL)

    def _convert_markdown_tables(self, text: str) -> str:
        """将 Markdown 表格转为 RST grid table，避免 |---| 被当成替换引用。"""
        lines = text.splitlines()
        result = []
        i = 0
        while i < len(lines):
            line = lines[i]
            if re.match(r'^\s', line):
                result.append(line)
                i += 1
                continue
            if '|' in line and i + 1 < len(lines) and re.match(r'^\s*\|?[\s:\-|]+\|', lines[i + 1]):
                table_lines = [line]
                i += 1
                while i < len(lines) and '|' in lines[i]:
                    table_lines.append(lines[i])
                    i += 1
                result.append(self._markdown_table_to_grid(table_lines))
                result.append('')
                continue
            result.append(line)
            i += 1
        return '\n'.join(result)

    @staticmethod
    def _parse_md_table_row(line: str) -> list:
        line = line.strip()
        if line.startswith('|'):
            line = line[1:]
        if line.endswith('|'):
            line = line[:-1]
        return [cell.strip() for cell in line.split('|')]

    def _convert_table_cell(self, text: str) -> str:
        text = self._convert_links(text)
        text = self._convert_inline_code(text)
        return text

    @staticmethod
    def _format_grid_cell(text: str, col_width: int) -> str:
        pad = col_width + 1 - _text_display_width(text)
        return ' ' + text + ' ' * max(pad, 1)

    @staticmethod
    def _grid_table_border(col_widths, char='-') -> str:
        return '+' + '+'.join(char * (w + 2) for w in col_widths) + '+'

    def _grid_table_row(self, cells, col_widths) -> str:
        parts = [self._format_grid_cell(c, col_widths[i]) for i, c in enumerate(cells)]
        return '|' + '|'.join(parts) + '|'

    def _markdown_table_to_grid(self, table_lines: list) -> str:
        parsed = [self._parse_md_table_row(l) for l in table_lines]
        if len(parsed) < 2:
            return '\n'.join(table_lines)
        header = parsed[0]
        body = parsed[2:] if len(parsed) > 2 else []
        all_rows = [header] + body
        num_cols = max(len(r) for r in all_rows)
        for row in all_rows:
            while len(row) < num_cols:
                row.append('')
        converted = [[self._convert_table_cell(c) for c in row] for row in all_rows]
        col_widths = [0] * num_cols
        for row in converted:
            for i, cell in enumerate(row):
                col_widths[i] = max(col_widths[i], _text_display_width(cell))
        out = [self._grid_table_border(col_widths, '-')]
        out.append(self._grid_table_row(converted[0], col_widths))
        out.append(self._grid_table_border(col_widths, '='))
        for row in converted[1:]:
            out.append(self._grid_table_row(row, col_widths))
            out.append(self._grid_table_border(col_widths, '-'))
        return '\n'.join(out)

    @staticmethod
    def _is_grid_table_line(line: str) -> bool:
        s = line.strip()
        if not s:
            return False
        if s.startswith('+') and re.match(r'^\+[-=+]+\+$', s):
            return True
        return s.startswith('|') and s.endswith('|')

    def _clean_text_before_conversion(self, text: str) -> str:
        """在转换前清理文本，移除可能导致问题的内容"""
        # 移除编辑器相关的标记或注释
        text = re.sub(r'用户\d+\s+复制\s+删除\s+', '', text)
        # 移除多余的空行
        text = re.sub(r'\n{3,}', '\n\n', text)
        return text

    def _convert_inline_code(self, text: str) -> str:
        return re.sub(r'`([^`]+)`', r'``\1``', text)

    def _convert_unordered_list(self, text: str) -> str:
        return re.sub(r'^[\-\*]\s+', r'* ', text, flags=re.MULTILINE)

    def _convert_ordered_list(self, text: str) -> str:
        # 保持原始数字格式，不转换为#.格式
        return text

def translate_md2rst(src_path, dst_path, lan):
    src_file = ""
    dst_file = ""

    if lan == 'en':
        src_file = "README.md"
        dst_file = "index.rst"

    elif lan == 'zh_CN':
        src_file = "README_CN.md"
        dst_file = "index.rst"
    else:
        return

    # 检查源文件是否存在
    if not os.path.isfile(os.path.join(src_path, src_file)):
        return

    # 使用类的方法进行文件转换
    converter = MarkdownToRST(src_path, dst_path)
    converter.convert_file(src_file, dst_file)


# 快速入门页只抽这些二级标题；概述 / 文档 / 硬件 / 参考工程等留在仓库 README。
HOMEPAGE_H2 = {
	"zh_CN": (
		"获取代码",
		"编译环境安装",
		"编译工程",
		"烧录固件",
	),
	"en": (
		"Get the Code",
		"Set Up the Build Environment",
		"Build the Project",
		"Flash the Firmware",
	),
}


def extract_readme_homepage_md(md_text, lan):
	"""从仓库 README 抽出指定二级节，供官网快速入门 include。"""
	keep = set(HOMEPAGE_H2.get(lan, ()))
	lines = md_text.splitlines(keepends=True)
	out = []
	i = 0
	while i < len(lines) and not lines[i].startswith("## "):
		i += 1
	while i < len(lines):
		m = re.match(r"^## (.+?)\s*$", lines[i])
		title = m.group(1).strip() if m else ""
		start = i
		i += 1
		while i < len(lines) and not lines[i].startswith("## "):
			i += 1
		if title in keep:
			out.extend(lines[start:i])
	return "".join(out)


def drop_companion_flash_pointer(md_text):
	"""快速入门第 5 节已有 3515 烧录步骤，抽出的第 4 节不显示 README 里的转链入口。不改方案 README。"""
	keep = []
	for line in md_text.splitlines(keepends=True):
		if "3515n_controller" in line and (
			"单独烧录" in line or "flashing its own firmware separately" in line
		):
			continue
		keep.append(line)
	return re.sub(r"\n{3,}", "\n\n", "".join(keep))


def generate_solution_homepage(chip_docs_path, lan):
	"""docs/README*.md → <lang>/_readme_body.rst，供快速入门 include。"""
	if lan not in ("en", "zh_CN"):
		return
	src_dir = os.path.normpath(os.path.join(chip_docs_path, ".."))
	dst_dir = os.path.join(chip_docs_path, lan)
	src_file = "README.md" if lan == "en" else "README_CN.md"
	src_path = os.path.join(src_dir, src_file)
	if not os.path.isfile(src_path):
		return
	os.makedirs(dst_dir, exist_ok=True)
	with open(src_path, "r", encoding="utf-8") as f:
		md_text = drop_companion_flash_pointer(
			extract_readme_homepage_md(f.read(), lan)
		)
	converter = MarkdownToRST(src_dir, dst_dir)
	rst = converter.convert(md_text)
	rst = re.sub(r"\.\./projects/([^/\s>`]+)/?", r"projects/\1/index.html", rst)
	rst = re.sub(
		r"^(获取代码|Get the Code)\n([=\-~`:'\"^+*#<>]{3,})",
		r".. _quick-start:\n\n\1\n\2",
		rst,
		count=1,
		flags=re.MULTILINE,
	)
	# 仅快速入门页加序号，不改方案 README。
	for i, title in enumerate(HOMEPAGE_H2.get(lan, ()), 1):
		numbered = f"{i}. {title}"
		rst = re.sub(
			rf"^{re.escape(title)}\n([=\-~`:'\"^+*#<>]{{3,}})",
			lambda m, n=numbered: f"{n}\n{m.group(1)[0] * _cjk_display_width(n)}",
			rst,
			count=1,
			flags=re.MULTILINE,
		)
	# README 三级标题不进侧栏，只作页内小标题。
	rst = re.sub(
		r"^(.+)\n[,]{3,}\s*\n",
		r".. rubric:: \1\n\n",
		rst,
		flags=re.MULTILINE,
	)
	body_path = os.path.join(dst_dir, "_readme_body.rst")
	with open(body_path, "w", encoding="utf-8") as f:
		f.write(rst)
	print(f"generate_solution_homepage: {src_file} -> {body_path}")


def copy_projects_doc(src_path, dst_path, lan):
    print(f"copy_projects_doc: {src_path} -> {dst_path}")
    if not os.path.isdir(src_path):
        return 0

    has_doc = False
    has_cmakelist = 0

    # 检查当前文件夹是否为projects，如果是则设置has_doc = True
    if os.path.basename(src_path) == 'projects':
        has_doc = True

    for item in os.listdir(src_path):
        item_path = os.path.join(src_path, item)
        if os.path.isfile(item_path):
            if item == "README.md" and lan == 'en':
                has_doc = True
            elif item == "README_CN.md" and lan == 'zh_CN':
                has_doc = True
            elif item == "projects.rst":
                has_doc = True

            if item == "CMakeLists.txt":
                has_cmakelist = 1

    if has_doc == False:
        return 0

    run_cmd(f'mkdir -p {dst_path}')
    translate_md2rst(src_path, dst_path, lan)

    if has_cmakelist == 1:
        return 1

    for item in os.listdir(src_path):
        item_path = os.path.join(src_path, item)
        item_dst_path = os.path.join(dst_path, item)
        if os.path.isdir(item_path):
            has_cmakelist = has_cmakelist + copy_projects_doc(item_path, item_dst_path, lan)

    if (has_cmakelist == 0):
        run_cmd(f'rm -rf {dst_path}')

    return has_cmakelist

def run_cmd(cmd):
	process = subprocess.Popen(cmd, shell=True)
	process.wait()

	return process

def log_error(log):
	print(PRINT_READ + log + PRINT_RESET)

def print_error_lines(file_path, error_log):
	ret = False

	try:
		with open(file_path, 'r') as file:
			for line in file:
				if error_log in line:
					if ret is False:
						log_error(f"Error found in file: {file}")
						ret = True

					log_error(line.strip())

	except FileNotFoundError:
		log_error("File not found!")
		ret = True
	except Exception as e:
		log_error("An error occurred: " + str(e))
		ret = True

	return ret


def latex_error_check(path):
	#print("check path: " + path)
	ret = False

	files = glob.glob(f"{path}/*.log")

	for file in files:
		ret = print_error_lines(file, "Error:")

	return ret


def build_armino_doc(source_path, dest_path, build_path, landir, version):
	print("found souce: " + source_path + " dest: " + dest_path + " build: " + build_path)
	command = "make -C " + source_path + " arminodocs -j32 " + "TARGET_DIR=" + dest_path + " TARGET_VERSION=" + version
	print("\t" + command)


	#clean before build
	run_cmd(f'rm -rf {build_path}/{landir}')
	run_cmd(f'rm -rf {source_path}/source')

	run_cmd(command)

	if latex_error_check(f'{source_path}/_build/latex') is True:
		log_error("### Build Docs Error, Exit ###")
		exit(-1)

	#copy
	run_cmd(f'cp -rf {dest_path} {build_path}/{landir}')
  
	#clean after build
	run_cmd(f'rm -rf {source_path}/xml')
	run_cmd(f'rm -rf {source_path}/xml_in')
	run_cmd(f'rm -rf {source_path}/man')
	run_cmd(f'rm -rf {source_path}/../__pycache__')
	run_cmd(f'rm -rf {dest_path}')
	run_cmd(f'rm -rf {dest_path} {build_path}/{landir}/inc')
	run_cmd(f'rm -rf {dest_path} {build_path}/{landir}/latex')

def build_html(source_path, dest_path, build_path, landir):
	print("found souce: " + source_path + " dest: " + dest_path + " build: " + build_path)
	command = "make -C " + source_path + " arminodocs -j32 " + "TARGET_DIR=" + dest_path
	print("\t" + command)


	#clean before build
	run_cmd(f'rm -rf {build_path}/{landir}')

	if run_cmd(command) is not True:
		log_error("### Build Docs Error, Exit ###")
		exit(-1)

	#copy
	run_cmd(f'cp -rf {dest_path} {build_path}/{landir}')
	#run_cmd(f'cp -rf {build_path}/{landir}/latex/AVDKDocument.pdf {build_path}/{landir}/AVDKDocument.pdf')
  
	#clean after build
	#run_cmd(f'rm -rf {source_path}/xml')
	#run_cmd(f'rm -rf {source_path}/xml_in')
	#run_cmd(f'rm -rf {source_path}/man')
	#run_cmd(f'rm -rf {docs_path}/__pycache__')
	#run_cmd(f'rm -rf {dest_path}')
	#run_cmd(f'rm -rf {dest_path} {build_path}/{landir}/inc')
	#run_cmd(f'rm -rf {dest_path} {build_path}/{landir}/latex')

def build_pdf(source_path, dest_path, build_path, landir):
	print("found souce: " + source_path + " dest: " + dest_path + " build: " + build_path)
	command = "make -C " + source_path + " latexpdf " + "TARGET_DIR=" + dest_path
	print("\t" + command)


	#clean before build
	run_cmd(f'rm -rf {build_path}/{landir}')

	run_cmd(command)

	#copy
	run_cmd(f'cp -rf {dest_path} {build_path}/{landir}')
	#run_cmd(f'cp -rf {build_path}/{landir}/latex/AVDKDocument.pdf {build_path}/{landir}/AVDKDocument.pdf')
  
	#clean after build
	#run_cmd(f'rm -rf {source_path}/xml')
	#run_cmd(f'rm -rf {source_path}/xml_in')
	#run_cmd(f'rm -rf {source_path}/man')
	#run_cmd(f'rm -rf {docs_path}/__pycache__')
	#run_cmd(f'rm -rf {dest_path}')
	#run_cmd(f'rm -rf {dest_path} {build_path}/{landir}/inc')
	#run_cmd(f'rm -rf {dest_path} {build_path}/{landir}/latex')

def build_doc(target, docs_path, build_path, version):
	print("build %s docs" % target)

	subdirectories = [d for d in os.listdir(docs_path) if os.path.isdir(os.path.join(docs_path, d))]
	white_list = {"en", "zh_CN"}
	target_dirs = [x for x in subdirectories if x in white_list]

	if not os.path.exists(build_path):
		run_cmd(f'mkdir -p {build_path}')

	for subdir in target_dirs:
		if target == 'build' :
			return

		copy_projects_doc(f'{docs_path}/../../projects', f'{docs_path}/{subdir}/projects', f'{subdir}')
		generate_solution_homepage(docs_path, subdir)

		source_path = docs_path + "/" + subdir
		#dest_path = build_path + "/" + target + "/" + subdir
		dest_path = docs_path + "/" + subdir + "/_build"
		#build_html(source_path, dest_path, build_path, subdir)
		#build_pdf(source_path, dest_path, build_path, subdir)
		build_armino_doc(source_path, dest_path, build_path, subdir, version)

def build_all(docs_path, build_path, version):
	print("build all docs")

	subdirectories = [d for d in os.listdir(docs_path) if os.path.isdir(os.path.join(docs_path, d))]
	black_list = {"common", ".git"}
	target_dirs = [x for x in subdirectories if x not in black_list]

	for subdir in target_dirs:
		print("found target: " + subdir)
		build_doc(subdir, docs_path + "/" + subdir, build_path + "/" + subdir, version)

def main(argv):
	parser = argparse.ArgumentParser()
	parser.add_argument('--clean', type=bool, default=False)
	parser.add_argument('--target', type=str, default="all")
	parser.add_argument('--type', type=str, default="html")
	parser.add_argument('--version', type=str, default="v3.1.1")
	args = parser.parse_args()

	root_path = os.getcwd()
	if 'ARMINO_SOC' in os.environ:
		soc_name = os.getenv('ARMINO_SOC')
	else:
		raise RuntimeError("not get soc name")
	build_path = root_path + f"/build/doc"
	if os.path.exists(build_path) == False:
		os.makedirs(build_path, exist_ok=True)

	if args.clean and args.target == "all":
		return

	run_cmd(f'cp {root_path}/version.json {build_path}/version.json')

	if args.clean == False and args.target == "all":
		build_all(root_path, build_path, args.version)
		return
 
	if os.path.exists(root_path + args.target):
		build_doc(args.target, root_path + args.target, build_path, args.version)
	else:
		print("Error not found target: %s" % (root_path + args.target))

if __name__ == "__main__":
	main(sys.argv)
