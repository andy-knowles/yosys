/*
 *  yosys -- Yosys Open SYnthesis Suite
 *
 *  Copyright (C) 2012  Clifford Wolf <clifford@clifford.at>
 *                2019  Eddie Hung    <eddie@fpgeh.com>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include "kernel/yosys.h"
#include "kernel/sigtools.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct MuxpackWorker
{
	Module *module;
	SigMap sigmap;

	int mux_count, pmux_count;

	pool<Cell*> remove_cells;

	dict<SigSpec, Cell*> sig_chain_next;
	dict<SigSpec, Cell*> sig_chain_prev;
	pool<SigBit> sigbit_with_non_chain_users;
	pool<Cell*> chain_start_cells;

	void make_sig_chain_next_prev()
	{
		for (auto wire : module->wires())
		{
			if (wire->port_output || wire->get_bool_attribute("\\keep")) {
				for (auto bit : sigmap(wire)) {
					sigbit_with_non_chain_users.insert(bit);
                }
			}
		}

		for (auto cell : module->cells())
		{
			if (cell->type.in("$mux") && !cell->get_bool_attribute("\\keep"))
			{
				SigSpec a_sig = sigmap(cell->getPort("\\A"));
				SigSpec y_sig = sigmap(cell->getPort("\\Y"));
   
				if (sig_chain_next.count(a_sig))
                    for (auto a_bit : a_sig.bits())
                        sigbit_with_non_chain_users.insert(a_bit);
                else
					sig_chain_next[a_sig] = cell;

				sig_chain_prev[y_sig] = cell;
                continue;
			}

			for (auto conn : cell->connections())
				if (cell->input(conn.first))
					for (auto bit : sigmap(conn.second))
						sigbit_with_non_chain_users.insert(bit);
		}
	}

	void find_chain_start_cells()
	{
		for (auto it : sig_chain_next)
		{
            for (auto bit : it.first.bits())
                if (sigbit_with_non_chain_users.count(bit))
                    goto start_cell;

			if (sig_chain_prev.count(it.first) != 0)
			{
				Cell *c1 = sig_chain_prev.at(it.first);
				Cell *c2 = it.second;

				if (c1->type != c2->type)
					goto start_cell;

				if (c1->parameters != c2->parameters)
					goto start_cell;

				continue;
			}

		start_cell:
			chain_start_cells.insert(it.second);
		}
	}

	vector<Cell*> create_chain(Cell *start_cell)
	{
		vector<Cell*> chain;

		Cell *c = start_cell;
		while (c != nullptr)
		{
			chain.push_back(c);

			SigSpec y_sig = sigmap(c->getPort("\\Y"));

			if (sig_chain_next.count(y_sig) == 0)
				break;

			c = sig_chain_next.at(y_sig);
			if (chain_start_cells.count(c) != 0)
				break;
		}

		return chain;
	}

	void process_chain(vector<Cell*> &chain)
	{
		if (GetSize(chain) < 2)
			return;

		int cursor = 0;
		while (cursor < GetSize(chain))
		{
			int cases = GetSize(chain) - cursor;

			Cell *first_cell = chain[cursor];
			dict<int, SigBit> taps_dict;

			if (cases < 2) {
				cursor++;
				continue;
			}

			Cell *last_cell = chain[cursor+cases-1];

			log("Converting %s.%s ... %s.%s to a pmux with %d cases.\n",
				log_id(module), log_id(first_cell), log_id(module), log_id(last_cell), cases);

			mux_count += cases;
			pmux_count += 1;

			first_cell->type = "$pmux";
            SigSpec b_sig = first_cell->getPort("\\B");
            SigSpec s_sig = first_cell->getPort("\\S");

			for (int i = 1; i < cases; i++) {
                Cell* cursor_cell = chain[cursor+i];
                b_sig.append(cursor_cell->getPort("\\B"));
                s_sig.append(cursor_cell->getPort("\\S"));
				remove_cells.insert(cursor_cell);
            }

			first_cell->setPort("\\B", b_sig);
			first_cell->setPort("\\S", s_sig);
            first_cell->setParam("\\S_WIDTH", GetSize(s_sig));
			first_cell->setPort("\\Y", last_cell->getPort("\\Y"));

			cursor += cases;
		}
	}

	void cleanup()
	{
		for (auto cell : remove_cells)
			module->remove(cell);

		remove_cells.clear();
		sig_chain_next.clear();
		sig_chain_prev.clear();
		chain_start_cells.clear();
	}

	MuxpackWorker(Module *module) :
			module(module), sigmap(module), mux_count(0), pmux_count(0)
	{
		make_sig_chain_next_prev();
		find_chain_start_cells();

		for (auto c : chain_start_cells) {
			vector<Cell*> chain = create_chain(c);
			process_chain(chain);
		}

		cleanup();
	}
};

struct MuxpackPass : public Pass {
	MuxpackPass() : Pass("muxpack", "TODO") { }
	void help() YS_OVERRIDE
	{
		//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
		log("\n");
		log("    muxpack [options] [selection]\n");
		log("\n");
        log("TODO");
		log("\n");
	}
	void execute(std::vector<std::string> args, RTLIL::Design *design) YS_OVERRIDE
	{
		log_header(design, "Executing MUXPACK pass (TODO).\n");

		size_t argidx;
		for (argidx = 1; argidx < args.size(); argidx++)
		{
			break;
		}
		extra_args(args, argidx, design);

		int mux_count = 0;
		int pmux_count = 0;

		for (auto module : design->selected_modules()) {
			MuxpackWorker worker(module);
			mux_count += worker.mux_count;
			pmux_count += worker.pmux_count;
		}

		log("Converted %d (p)mux cells into %d pmux cells.\n", mux_count, pmux_count);
	}
} MuxpackPass;

PRIVATE_NAMESPACE_END
