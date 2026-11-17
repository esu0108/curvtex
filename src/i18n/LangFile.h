#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// 言語ファイル (lang/*.lang) の解析。Windows 依存なし。
//   @name = "日本語"          ← ヘッダー (@ で始まるキー。@order = 10 のように引用符なしでもよい)
//   MenuFile = "ファイル"      ← 文字列 (キー = "文字列")。# で始まる行はコメント
// 文字列は "" で囲み、\\ \" \n \t \r \uXXXX のエスケープが使える。閉じる " の後ろは空白かコメントだけ。

struct LangFileData {
    std::string              name;  // @name
    std::vector<std::string> match; // @match (空白区切り・小文字)
    std::string              font;  // @font (小文字)
    int                      order = 1000; // @order
    std::vector<std::pair<std::string, std::string>> entries; // (キー, UTF-8 の文字列) をファイルの順に
};

// text (UTF-8。先頭の BOM は無視) を解析する。読めない行は warnings に「label:行番号: 理由」を足して読み飛ばす。
void ParseLangText(std::string_view text, const std::string& label, LangFileData& out, std::vector<std::string>& warnings);

// printf の変換指定を並びのまま取り出す ("%%" は除く)。書式の食い違いを調べるのに使う。
std::vector<std::string> PrintfSpecs(std::string_view s);

// OS の表示言語タグ (例: "zh-TW") に合う言語を選ぶ。matches[i] は i 番目の言語の @match。見つからなければ -1。
//   1) タグが完全に一致  2) タグの先頭部分 (zh-TW → zh) が一致  3) 先頭部分が同じ言語がある (pt-PT → pt-br)
int MatchLanguage(std::string tag, const std::vector<std::vector<std::string>>& matches);
