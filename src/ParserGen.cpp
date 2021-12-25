/*-------------------------------------------------------------------------
ParserGen -- Generation of the Recursive Descent Parser
Compiler Generator Coco/R,
Copyright (c) 1990, 2004 Hanspeter Moessenboeck, University of Linz
ported to C++ by Csaba Balazs, University of Szeged
extended by M. Loeberbauer & A. Woess, Univ. of Linz
with improvements by Pat Terry, Rhodes University

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

As an exception, it is allowed to write an extension of Coco/R that is
used as a plugin in non-free software.

If not otherwise stated, any source code generated by Coco/R (other than
Coco/R itself) does not fall under the GNU General Public License.
-------------------------------------------------------------------------*/

#include <ctype.h>
#include "ParserGen.h"
#include "Parser.h"
#include "BitArray.h"
#include "Generator.h"

namespace Coco {

void ParserGen::Indent (int n) {
	for (int i = 1; i <= n; i++) fputws(_SC("\t"), gen);
}

// use a switch if more than 5 alternatives and none starts with a resolver, and no LL1 warning
bool ParserGen::UseSwitch (const Node *p) {
	BitArray *s2;
	if (p->typ != Node::alt) return false;
	int nAlts = 0;
	BitArray s1(tab->terminals.Count);
	while (p != NULL) {
		s2 = tab->Expected0(p->sub, curSy);
		// must not optimize with switch statement, if there are ll1 warnings
		if (s1.Overlaps(s2)) {delete s2; return false; }
		s1.Or(s2);
		delete s2;
		++nAlts;
		// must not optimize with switch-statement, if alt uses a resolver expression
		if (p->sub->typ == Node::rslv) return false;
		p = p->down;
	}
	return nAlts > 5;
}

int ParserGen::GenNamespaceOpen(const wchar_t *nsName) {
	if (nsName == NULL || coco_string_length(nsName) == 0) {
		return 0;
	}
	const int len = coco_string_length(nsName);
	int startPos = 0;
	int nrOfNs = 0;
	do {
		int curLen = coco_string_indexof(nsName + startPos, COCO_CPP_NAMESPACE_SEPARATOR);
		if (curLen == -1) { curLen = len - startPos; }
		fwprintf(gen, _SC("namespace %.*") _SFMT _SC(" {\n"), curLen, nsName+startPos);
		startPos = startPos + curLen + 1;
		if (startPos < len && nsName[startPos] == COCO_CPP_NAMESPACE_SEPARATOR) {
			++startPos;
		}
		++nrOfNs;
	} while (startPos < len);
	return nrOfNs;
}

void ParserGen::GenNamespaceClose(int nrOfNs) {
	for (int i = 0; i < nrOfNs; ++i) {
		fputws(_SC("} // namespace\n"), gen);
	}
}

void ParserGen::CopySourcePart (const Position *pos, int indent) {
	// Copy text described by pos from atg to gen
	int ch, i;
	if (pos != NULL) {
		buffer->SetPos(pos->beg); ch = buffer->Read();
		if (tab->emitLines && pos->line) {
			fwprintf(gen, _SC("\n#line %d \"%") _SFMT _SC("\"\n"), pos->line, tab->srcName);
		}
		Indent(indent);
		while (buffer->GetPos() <= pos->end) {
			while (ch == CR || ch == LF) {  // eol is either CR or CRLF or LF
				fputws(_SC("\n"), gen); Indent(indent);
				if (ch == CR) { ch = buffer->Read(); } // skip CR
				if (ch == LF) { ch = buffer->Read(); } // skip LF
				for (i = 1; i <= pos->col && (ch == ' ' || ch == '\t'); i++) {
					// skip blanks at beginning of line
					ch = buffer->Read();
				}
				if (buffer->GetPos() > pos->end) goto done;
			}
			fwprintf(gen, _SC("%") _CHFMT, ch);
			ch = buffer->Read();
		}
		done:
		if (indent > 0) fputws(_SC("\n"), gen);
	}
}

void ParserGen::GenErrorMsg (int errTyp, const Symbol *sym) {
	errorNr++;
	const size_t formatLen = 1000;
	wchar_t format[formatLen];
	coco_swprintf(format, formatLen, _SC("\t\t\tcase %d: s = _SC(\""), errorNr);
	coco_string_merge(err, format);
	if (errTyp == tErr) {
		if (sym->name[0] == _SC('"')) {
			wchar_t *se = tab->Escape(sym->name);
			coco_swprintf(format, formatLen, _SC("%") _SFMT _SC(" expected"), se);
			coco_string_merge(err, format);
			coco_string_delete(se);
		} else {
			coco_swprintf(format, formatLen, _SC("%") _SFMT _SC(" expected"), sym->name);
			coco_string_merge(err, format);
		}
	} else if (errTyp == altErr) {
		coco_swprintf(format, formatLen, _SC("invalid %") _SFMT, sym->name);
		coco_string_merge(err, format);
	} else if (errTyp == syncErr) {
		coco_swprintf(format, formatLen, _SC("this symbol not expected in %") _SFMT, sym->name);
		coco_string_merge(err, format);
	}
	coco_swprintf(format, formatLen, _SC("\"); break;\n"));
	coco_string_merge(err, format);
}

int ParserGen::NewCondSet (const BitArray *s) {
	for (int i = 1; i < symSet.Count; i++) // skip symSet[0] (reserved for union of SYNC sets)
		if (Sets::Equals(s, symSet[i])) return i;
	symSet.Add(s->Clone());
	return symSet.Count - 1;
}

void ParserGen::GenCond (const BitArray *s, const Node *p) {
	if (p->typ == Node::rslv) CopySourcePart(p->pos, 0);
	else {
		int n = Sets::Elements(s);
		if (n == 0) fputws(_SC("false"), gen); // happens if an ANY set matches no symbol
		else if (n <= maxTerm) {
			Symbol *sym;
			for (int i=0; i<tab->terminals.Count; i++) {
				sym = (Symbol*)tab->terminals[i];
				if ((*s)[sym->n]) {
					fputws(_SC("IsKind(la, "), gen);
					WriteSymbolOrCode(gen, sym);
					fputws(_SC(")"), gen);
					--n;
					if (n > 0) fputws(_SC(" || "), gen);
				}
			}
		} else
			fwprintf(gen, _SC("StartOf(%d /* %s */)"), NewCondSet(s), (tab->nTyp[p->typ]));
	}
}

void ParserGen::PutCaseLabels (const BitArray *s0) {
	Symbol *sym;
	BitArray *s = DerivationsOf(s0);
	for (int i=0; i<tab->terminals.Count; i++) {
		sym = tab->terminals[i];
		if ((*s)[sym->n]) {
			fputws(_SC("case "), gen);
			WriteSymbolOrCode(gen, sym);
			fputws(_SC(": "), gen);
		}
	}
	delete s;
}

BitArray *ParserGen::DerivationsOf(const BitArray *s0) {
	BitArray *s = s0->Clone();
	bool done = false;
	while (!done) {
		done = true;
		for (int i=0; i<tab->terminals.Count; i++) {
			Symbol *sym = tab->terminals[i];
			if ((*s)[sym->n]) {
				for (int i=0; i<tab->terminals.Count; i++) {
					Symbol *baseSym = tab->terminals[i];
					if (baseSym->inherits == sym && !(*s)[baseSym->n]) {
						s->Set(baseSym->n, true);
						done = false;
					}
				}
			}
		}
	}
	return s;
}

void ParserGen::GenCode (const Node *p, int indent, BitArray *isChecked) {
	const Node *p2;
	BitArray *s1, *s2;
	while (p != NULL) {
		if (p->typ == Node::nt) {
			Indent(indent);
			fwprintf(gen, _SC("%") _SFMT _SC("_NT("), p->sym->name);
			CopySourcePart(p->pos, 0);
			fputws(_SC(");\n"), gen);
		} else if (p->typ == Node::t) {
			Indent(indent);
			// assert: if isChecked[p->sym->n] is true, then isChecked contains only p->sym->n
			if ((*isChecked)[p->sym->n]) {
				fputws(_SC("Get();\n"), gen);
			}
			else {
				fputws(_SC("Expect("), gen);
				WriteSymbolOrCode(gen, p->sym);
				fputws(_SC(");\n"), gen);
			}
			fputws(_SC("#ifdef PARSER_WITH_AST\n\tAstAddTerminal();\n#endif\n"), gen);
		} if (p->typ == Node::wt) {
			Indent(indent);
			s1 = tab->Expected(p->next, curSy);
			s1->Or(tab->allSyncSets);
			fputws(_SC("ExpectWeak("), gen);
			WriteSymbolOrCode(gen, p->sym);
			fwprintf(gen, _SC(", %d);\n"), NewCondSet(s1));
                        delete s1;
		} if (p->typ == Node::any) {
			Indent(indent);
			int acc = Sets::Elements(p->set);
			if (tab->terminals.Count == (acc + 1) || (acc > 0 && Sets::Equals(p->set, isChecked))) {
				// either this ANY accepts any terminal (the + 1 = end of file), or exactly what's allowed here
				fputws(_SC("Get();\n"), gen);
			} else {
				GenErrorMsg(altErr, curSy);
				if (acc > 0) {
					fputws(_SC("if ("), gen); GenCond(p->set, p); fwprintf(gen, _SC(") Get(); else SynErr(%d);\n"), errorNr);
				} else fwprintf(gen, _SC("SynErr(%d); // ANY node that matches no symbol\n"), errorNr);
			}
		} if (p->typ == Node::eps) {	// nothing
		} if (p->typ == Node::rslv) {	// nothing
		} if (p->typ == Node::sem) {
			CopySourcePart(p->pos, indent);
		} if (p->typ == Node::sync) {
			Indent(indent);
			GenErrorMsg(syncErr, curSy);
			s1 = p->set->Clone();
			fputws(_SC("while (!("), gen); GenCond(s1, p); fputws(_SC(")) {"), gen);
			fwprintf(gen, _SC("SynErr(%d); Get();"), errorNr); fputws(_SC("}\n"), gen);
                        delete s1;
		} if (p->typ == Node::alt) {
			s1 = tab->First(p);
			bool equal = Sets::Equals(s1, isChecked);
                        delete s1;
			bool useSwitch = UseSwitch(p);
			if (useSwitch) { Indent(indent); fputws(_SC("switch (la->kind) {\n"), gen); }
			p2 = p;
			while (p2 != NULL) {
				s1 = tab->Expected(p2->sub, curSy);
				Indent(indent);
				if (useSwitch) {
					PutCaseLabels(s1); fputws(_SC("{\n"), gen);
				} else if (p2 == p) {
					fputws(_SC("if ("), gen); GenCond(s1, p2->sub); fputws(_SC(") {\n"), gen);
				} else if (p2->down == NULL && equal) { fputws(_SC("} else {\n"), gen);
				} else {
					fputws(_SC("} else if ("), gen);  GenCond(s1, p2->sub); fputws(_SC(") {\n"), gen);
				}
				GenCode(p2->sub, indent + 1, s1);
				if (useSwitch) {
					Indent(indent); fputws(_SC("\tbreak;\n"), gen);
					Indent(indent); fputws(_SC("}\n"), gen);
				}
				p2 = p2->down;
                                delete s1;
			}
			Indent(indent);
			if (equal) {
				fputws(_SC("}\n"), gen);
			} else {
				GenErrorMsg(altErr, curSy);
				if (useSwitch) {
					fwprintf(gen, _SC("default: SynErr(%d); break;\n"), errorNr);
					Indent(indent); fputws(_SC("}\n"), gen);
				} else {
					fputws(_SC("} "), gen); fwprintf(gen, _SC("else SynErr(%d);\n"), errorNr);
				}
			}
		} if (p->typ == Node::iter) {
			Indent(indent);
			p2 = p->sub;
			fputws(_SC("while ("), gen);
			if (p2->typ == Node::wt) {
				s1 = tab->Expected(p2->next, curSy);
				s2 = tab->Expected(p->next, curSy);
				fputws(_SC("WeakSeparator("), gen);
				WriteSymbolOrCode(gen, p2->sym);
				fwprintf(gen, _SC(",%d,%d) "), NewCondSet(s1), NewCondSet(s2));
                                delete s1;
                                delete s2;
				s1 = new BitArray(tab->terminals.Count);  // for inner structure
				if (p2->up || p2->next == NULL) p2 = NULL; else p2 = p2->next;
			} else {
				s1 = tab->First(p2);
				GenCond(s1, p2);
			}
			fputws(_SC(") {\n"), gen);
			GenCode(p2, indent + 1, s1);
			Indent(indent); fputws(_SC("}\n"), gen);
                        delete s1;
		} if (p->typ == Node::opt) {
			s1 = tab->First(p->sub);
			Indent(indent);
			fputws(_SC("if ("), gen); GenCond(s1, p->sub); fputws(_SC(") {\n"), gen);
			GenCode(p->sub, indent + 1, s1);
			Indent(indent); fputws(_SC("}\n"), gen);
                        delete s1;
		}
		if (p->typ != Node::eps && p->typ != Node::sem && p->typ != Node::sync)
			isChecked->SetAll(false);  // = new BitArray(Symbol.terminals.Count);
		if (p->up) break;
		p = p->next;
	}
}


void ParserGen::GenTokensHeader() {
	Symbol *sym;
	int i;
	bool isFirst = true;

	fputws(_SC("\tenum {\n"), gen);

	// tokens
	for (i=0; i<tab->terminals.Count; i++) {
		sym = tab->terminals[i];
		if (!isalpha(sym->name[0])) { continue; }

		if (isFirst) { isFirst = false; }
		else { fputws(_SC("\n"), gen); }

		fwprintf(gen , _SC("\t\t_%") _SFMT _SC("=%d,"), sym->name, sym->n);
		if(sym->inherits) {
                    fwprintf(gen , _SC(" // INHERITS -> %") _SFMT, sym->inherits->name);
                }
	}

	// pragmas
	for (i=0; i<tab->pragmas.Count; i++) {
		if (isFirst) { isFirst = false; }
		else { fputws(_SC("\n"), gen); }

		sym = tab->pragmas[i];
		fwprintf(gen , _SC("\t\t_%") _SFMT _SC("=%d,"), sym->name, sym->n);
	}

	fputws(_SC("\n\t};\n"), gen);

        // nonterminals
        fputws(_SC("#ifdef PARSER_WITH_AST\n\tenum eNonTerminals{\n"), gen);
        isFirst = true;
        for (i=0; i<tab->nonterminals.Count; i++) {
                sym = tab->nonterminals[i];
                if (isFirst) { isFirst = false; }
                else { fputws(_SC(",\n"), gen); }

                fwprintf(gen , _SC("\t\t_%") _SFMT _SC("=%d"), sym->name, sym->n);
        }
        fputws(_SC("\n\t};\n#endif\n"), gen);

}

void ParserGen::GenCodePragmas() {
	Symbol *sym;
	for (int i=0; i<tab->pragmas.Count; i++) {
		sym = tab->pragmas[i];
		fputws(_SC("\t\tif (la->kind == "), gen);
		WriteSymbolOrCode(gen, sym);
		fputws(_SC(") {\n"), gen);
		CopySourcePart(sym->semPos, 4);
		fputws(_SC("\t\t}\n"), gen);
	}
}

void ParserGen::GenTokenBase() {
	Symbol *sym;
	fwprintf(gen, _SC("\tstatic const int tBase[%d] = {"), tab->terminals.Count);

	for (int i=0; i<tab->terminals.Count; i++) {
		sym = tab->terminals[i];
                if((i % 20) == 0) fputws(_SC("\n\t\t"), gen);
                if (sym->inherits == NULL)
                        fputws(_SC("-1,"), gen); // not inherited
                else
                        fwprintf(gen, _SC("%d,"), sym->inherits->n);
	}
	fputws(_SC("\n\t};\n"), gen);
}

void ParserGen::WriteSymbolOrCode(FILE *gen, const Symbol *sym) {
	if (!isalpha(sym->name[0])) {
		fwprintf(gen, _SC("%d /* %") _SFMT _SC(" */"), sym->n, sym->name);
	} else {
		fwprintf(gen, _SC("_%") _SFMT, sym->name);
	}
}

void ParserGen::GenProductionsHeader() {
	Symbol *sym;
	for (int i=0; i<tab->nonterminals.Count; i++) {
		sym = tab->nonterminals[i];
		curSy = sym;
		fwprintf(gen, _SC("\tvoid %") _SFMT _SC("_NT("), sym->name);
		CopySourcePart(sym->attrPos, 0);
		fputws(_SC(");\n"), gen);
	}
}

void ParserGen::GenProductions() {
	Symbol *sym;
        BitArray ba(tab->terminals.Count);
	for (int i=0; i<tab->nonterminals.Count; i++) {
		sym = tab->nonterminals[i];
		curSy = sym;
		fwprintf(gen, _SC("void Parser::%") _SFMT _SC("_NT("), sym->name);
		CopySourcePart(sym->attrPos, 0);
		fputws(_SC(") {\n"), gen);
		CopySourcePart(sym->semPos, 2);
                fputws(_SC("#ifdef PARSER_WITH_AST\n"), gen);
                if(i == 0) fwprintf(gen, _SC("\t\tToken *ntTok = new Token(); ntTok->kind = eNonTerminals::_%") _SFMT _SC("; ntTok->line = 0; ntTok->val = coco_string_create(_SC(\"%") _SFMT _SC("\"));ast_root = new SynTree( ntTok ); ast_stack.Clear(); ast_stack.Add(ast_root);\n"), sym->name, sym->name);
                else {
                        fwprintf(gen, _SC("\t\tbool ntAdded = AstAddNonTerminal(eNonTerminals::_%") _SFMT _SC(", _SC(\"%") _SFMT _SC("\"), la->line);\n"), sym->name, sym->name);
                }
                fputws(_SC("#endif\n"), gen);
                ba.SetAll(false);
		GenCode(sym->graph, 2, &ba);
                fputws(_SC("#ifdef PARSER_WITH_AST\n"), gen);
                if(i == 0) fputws(_SC("\t\tAstPopNonTerminal();\n"), gen);
                else fputws(_SC("\t\tif(ntAdded) AstPopNonTerminal();\n"), gen);
                fputws(_SC("#endif\n}\n\n"), gen);
	}
}

void ParserGen::InitSets() {
	fwprintf(gen, _SC("\tstatic const bool set[%d][%d] = {\n"), symSet.Count, tab->terminals.Count+1);

	for (int i = 0; i < symSet.Count; i++) {
		BitArray *s = DerivationsOf(symSet[i]);
		fputws(_SC("\t\t{"), gen);
		int j = 0;
		Symbol *sym;
		for (int k=0; k<tab->terminals.Count; k++) {
			sym = tab->terminals[k];
			fputws(((*s)[sym->n]) ? _SC("T,") : _SC("x,"), gen);
			++j;
			if (j%4 == 0) fputws(_SC(" "), gen);
		}
		if (i == symSet.Count-1) fputws(_SC("x}\n"), gen); else fputws(_SC("x},\n"), gen);
		delete s;
	}
	fputws(_SC("\t};\n\n"), gen);
}

int ParserGen::GenCodeRREBNF (const Node *p, int depth) {
        int rc = 0, loop_count = 0;
        const Node *p2;
        while (p != NULL) {
                switch (p->typ) {
                        case Node::nt:
                        case Node::t: {
                                fputws(_SC(" "), gen);
                                fputws(p->sym->name, gen);
                                ++rc;
                                break;
                        }
                        case Node::wt: {
                                break;
                        }
                        case Node::any: {
                                fputws(_SC(" ANY"), gen);
                                break;
                        }
                        case Node::eps: break; // nothing
                        case Node::rslv: break; // nothing
                        case Node::sem: {
                                break;
                        }
                        case Node::sync: {
                                break;
                        }
                        case Node::alt: {
				bool need_close_alt = false;
				if(depth > 0 || loop_count || p->next) {
					fputws(" (", gen);
					need_close_alt = true;
				}
                                p2 = p;
                                while (p2 != NULL) {
                                        rc += GenCodeRREBNF(p2->sub, depth+1);
                                        p2 = p2->down;
                                        if(p2 != NULL) fputws(_SC(" |"), gen);
                                }
                                if(need_close_alt) fputws(_SC(" )"), gen);
                                break;
                        }
                        case Node::iter: {
                                if(p->sub->up == 0) fputws(_SC(" ("), gen);
                                rc += GenCodeRREBNF(p->sub, depth+1);
                                if(p->sub->up == 0) fputws(_SC(" )"), gen);
                                fputws(_SC("*"), gen);
                                break;
                        }
                        case Node::opt:
                                if(p->sub->up == 0) fputws(_SC(" ("), gen);
                                rc += GenCodeRREBNF(p->sub, depth+1);
                                if(p->sub->up == 0) fputws(_SC(" )"), gen);
                                fputws(_SC("?"), gen);
                                break;
                }
                if (p->up) break;
                p = p->next;
		++loop_count;
        }
        return rc;
}

void ParserGen::WriteRREBNF () {
	Symbol *sym;
        Generator g(tab, errors);
        gen = g.OpenGen(_SC("Parser.ebnf"));

        fwprintf(gen, _SC("//\n// EBNF generated by CocoR parser generator to be viewed with https://www.bottlecaps.de/rr/ui\n//\n"));
        fwprintf(gen, _SC("\n//\n// productions\n//\n\n"));
	for (int i=0; i<tab->nonterminals.Count; i++) {
		sym = tab->nonterminals[i];
                fwprintf(gen, _SC("%s ::= "), sym->name);
                if(GenCodeRREBNF(sym->graph, 0) == 0) {
                        fputws(_SC("\"\?\?()\?\?\""), gen);
                }
                fputws(_SC("\n"), gen);
        }
        fwprintf(gen, _SC("\n//\n// tokens\n//\n\n"));
        Iterator *iter = tab->literals.GetIterator();
	for (int i=0; i<tab->terminals.Count; i++) {
		sym = tab->terminals[i];
                if (isalpha(sym->name[0])) {
                    iter->Reset();
                    while (iter->HasNext()) {
                            DictionaryEntry *e = iter->Next();
                            if (e->val == sym) {
                                    fwprintf(gen, _SC("%s ::= %s\n"), sym->name, e->key);
                                    break;
                            }
                    }
                } else {
                        //fwprintf(gen, _SC("%d /* %s */"), sym->n, sym->name));
                }
        }
        delete iter;
        fclose(gen);
}

void ParserGen::WriteParser () {
	Generator g(tab, errors);
	int oldPos = buffer->GetPos();  // Pos is modified by CopySourcePart
	symSet.Add(tab->allSyncSets);

	fram = g.OpenFrame(_SC("Parser.frame"));
	gen = g.OpenGen(_SC("Parser.h"));

	Symbol *sym;
	for (int i=0; i<tab->terminals.Count; i++) {
		sym = tab->terminals[i];
		GenErrorMsg(tErr, sym);
	}

	g.GenCopyright();
	g.SkipFramePart(_SC("-->begin"));

	g.CopyFramePart(_SC("-->prefix"));
	g.GenPrefixFromNamespace();

	g.CopyFramePart(_SC("-->prefix"));
	g.GenPrefixFromNamespace();

	g.CopyFramePart(_SC("-->headerdef"));

	if (usingPos != NULL) {CopySourcePart(usingPos, 0); fputws(_SC("\n"), gen);}
	g.CopyFramePart(_SC("-->namespace_open"));
	int nrOfNs = GenNamespaceOpen(tab->nsName);

	g.CopyFramePart(_SC("-->constantsheader"));
	GenTokensHeader();  /* ML 2002/09/07 write the token kinds */
	fputws(_SC("\tint maxT;\n"), gen);
	g.CopyFramePart(_SC("-->declarations")); CopySourcePart(tab->semDeclPos, 0);
	g.CopyFramePart(_SC("-->productionsheader")); GenProductionsHeader();
	g.CopyFramePart(_SC("-->namespace_close"));
	GenNamespaceClose(nrOfNs);

	g.CopyFramePart(_SC("-->implementation"));
	fclose(gen);

	// Source
	gen = g.OpenGen(_SC("Parser.cpp"));

	g.GenCopyright();
	g.SkipFramePart(_SC("-->begin"));
	g.CopyFramePart(_SC("-->namespace_open"));
	nrOfNs = GenNamespaceOpen(tab->nsName);

	g.CopyFramePart(_SC("-->pragmas")); GenCodePragmas();
	g.CopyFramePart(_SC("-->tbase")); GenTokenBase(); // write all tokens base types
	g.CopyFramePart(_SC("-->productions")); GenProductions();
	g.CopyFramePart(_SC("-->parseRoot")); fwprintf(gen, _SC("\t%") _SFMT _SC("_NT();\n"), tab->gramSy->name); if (tab->checkEOF) fputws(_SC("\tExpect(0);"), gen);
	g.CopyFramePart(_SC("-->constants"));
	fwprintf(gen, _SC("\tmaxT = %d;\n"), tab->terminals.Count-1);
	g.CopyFramePart(_SC("-->initialization")); InitSets();
	g.CopyFramePart(_SC("-->errors")); fwprintf(gen, _SC("%") _SFMT, err);
	g.CopyFramePart(_SC("-->namespace_close"));
	GenNamespaceClose(nrOfNs);
	g.CopyFramePart(NULL);
	fclose(gen);
	buffer->SetPos(oldPos);
}


void ParserGen::WriteStatistics () {
	fwprintf(trace, _SC("\n%d terminals\n"), tab->terminals.Count);
	fwprintf(trace, _SC("%d symbols\n"), tab->terminals.Count + tab->pragmas.Count +
	                               tab->nonterminals.Count);
	fwprintf(trace, _SC("%d nodes\n"), tab->nodes.Count);
	fwprintf(trace, _SC("%d sets\n"), symSet.Count);
}


ParserGen::ParserGen (Parser *parser) {
	maxTerm = 3;
	CR = '\r';
	LF = '\n';
	tErr = 0;
	altErr = 1;
	syncErr = 2;
	tab = parser->tab;
	errors = parser->errors;
	trace = parser->trace;
	buffer = parser->scanner->buffer;
	errorNr = -1;
	usingPos = NULL;

	err = NULL;
}

ParserGen::~ParserGen () {
    for(int i=0; i<symSet.Count; ++i) delete symSet[i];
    delete usingPos;
    coco_string_delete(err);
}

}; // namespace
