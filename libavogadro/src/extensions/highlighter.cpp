/**********************************************************************
  Highlighter - Code highlighting for the python terminal

  Copyright (C) 2008 by Tim Vandermeersch

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.cc/>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 ***********************************************************************/

#include "highlighter.h"

#include <QRegularExpression>

namespace Avogadro {

  Highlighter::Highlighter(QTextDocument *parent) : QSyntaxHighlighter(parent)
  {
    HighlightingRule rule;

    promptFormat.setForeground(Qt::red);
    promptFormat.setFontWeight(QFont::Bold);
    QStringList promptPatterns;
    promptPatterns << ">>>" << "\\.\\.\\.";

    foreach (const QString &pattern, promptPatterns) {
        rule.pattern = QRegularExpression(pattern);
        rule.format = promptFormat;
        highlightingRules.append(rule);
    }

  
    
    keywordFormat.setForeground(Qt::darkGreen);
    keywordFormat.setFontWeight(QFont::Bold);
    QStringList keywordPatterns;

    // Python keywords
    keywordPatterns << "\\band\\b" << "\\bdel\\b" << "\\bfrom\\b"
                    << "\\bnot\\b" << "\\bwhile\\b" << "\\bas\\b"
                    << "\\belif\\b" << "\\bglobal\\b" << "\\bor\\b"
                    << "\\bwith\\b" << "\\bassert\\b" << "\\belse\\b"
                    << "\\bif\\b" << "\\bpass\\b" << "\\byield\\b"
                    << "\\bbreak\\b" << "\\bexcept\\b" << "\\bimport\\b"
                    << "\\bprint\\b" << "\\bclass\\b" << "\\bexec\\b"
                    << "\\bin\\b" << "\\braise\\b" << "\\bcontinue\\b"
                    << "\\bfinally\\b" << "\\bis\\b" << "\\breturn\\b"
                    << "\\bdef\\b" << "\\bfor\\b" << "\\blambda\\b"
                    << "\\btry\\b";

    foreach (const QString &pattern, keywordPatterns) {
        rule.pattern = QRegularExpression(pattern);
        rule.format = keywordFormat;
        highlightingRules.append(rule);
    }

    classFormat.setFontWeight(QFont::Bold);
    classFormat.setForeground(Qt::darkMagenta);
    rule.pattern = QRegularExpression("\\bQ[A-Za-z]+\\b");
    rule.format = classFormat;
    highlightingRules.append(rule);

    singleLineCommentFormat.setForeground(Qt::red);
    rule.pattern = QRegularExpression("#[^\n]*");
    rule.format = singleLineCommentFormat;
    highlightingRules.append(rule);

    multiLineCommentFormat.setForeground(Qt::red);

    quotationFormat.setForeground(Qt::darkGreen);
    rule.pattern = QRegularExpression("\".*\"");
    rule.format = quotationFormat;
    highlightingRules.append(rule);

    functionFormat.setFontItalic(true);
    functionFormat.setForeground(Qt::blue);
    rule.pattern = QRegularExpression("\\b[A-Za-z0-9_]+(?=\\()");
    rule.format = functionFormat;
    highlightingRules.append(rule);

    commentStartExpression = QRegularExpression("/\\*");
    commentEndExpression = QRegularExpression("\\*/");
  }

  void Highlighter::highlightBlock(const QString &text)
  {
    foreach (const HighlightingRule &rule, highlightingRules) {
        QRegularExpressionMatchIterator matches = rule.pattern.globalMatch(text);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
    setCurrentBlockState(0);

    int startIndex = 0;
    if (previousBlockState() != 1)
        startIndex = text.indexOf(commentStartExpression);

    while (startIndex >= 0) {
        QRegularExpressionMatch endMatch = commentEndExpression.match(text, startIndex);
        int commentLength;
        if (!endMatch.hasMatch()) {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        } else {
            commentLength = endMatch.capturedEnd() - startIndex;
        }
        setFormat(startIndex, commentLength, multiLineCommentFormat);
        startIndex = text.indexOf(commentStartExpression,
                                                startIndex + commentLength);
    }
  }

} // namespace

