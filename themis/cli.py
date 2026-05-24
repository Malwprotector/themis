# cli.py
import argparse
from collections import Counter
from .scanner import (
    build_plan,
    write_csv,
    read_csv,
    apply_plan,
    known_categories,
)


def _print_plan_summary(plan):
    """Print a compact summary that matches the current LDA + Bayes workflow."""
    total = len(plan)
    selected = sum(1 for item in plan if item.selected)
    by_model = Counter(getattr(item, 'model', 'unknown') for item in plan)
    by_category = Counter(getattr(item, 'category', '') or 'uncategorized' for item in plan)

    print(f'Files in plan: {total}')
    print(f'Marked for move: {selected}')

    if by_model:
        print('Models used:')
        for model, count in sorted(by_model.items()):
            print(f'  - {model}: {count}')

    if by_category:
        print('Top categories:')
        for category, count in by_category.most_common(10):
            print(f'  - {category}: {count}')


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog='themis',
        description='THEMIS 2.4'
    )
    sub = parser.add_subparsers(dest='cmd')

    sub.add_parser('gui', help='open the graphical interface')

    scan = sub.add_parser('scan', help='scan folders and create a proposed sorting plan')
    scan.add_argument('directories', nargs='+', help='one or more folders to scan')
    scan.add_argument('--target', help='target root folder for sorted files')
    scan.add_argument('--topics', type=int, default=8, help='number of LDA topics/groups, default: 8')
    scan.add_argument('--iterations', type=int, default=350, help='LDA Gibbs sampling iterations, default: 350')
    scan.add_argument('--bayes-threshold', type=float, default=0.68, help='minimum Bayes confidence required to override LDA, default: 0.68')
    scan.add_argument('--min-bayes-examples', type=int, default=3, help='minimum approved history examples before Bayes can train, default: 3')
    scan.add_argument('--min-confidence', type=float, default=0.0, help='minimum chosen model confidence for marking a row as selected, default: 0.0')
    scan.add_argument('--output', default='themis_plan.csv', help='CSV output path, default: themis_plan.csv')
    scan.add_argument('--no-recursive', action='store_true', help='scan only the top level of each folder')
    scan.add_argument('--include-hidden', action='store_true', help='include hidden files and folders')
    scan.add_argument('--apply', action='store_true', help='apply the generated plan immediately')

    ap = sub.add_parser('apply', help='apply a reviewed CSV plan and update Bayes training history')
    ap.add_argument('plan_csv', help='reviewed CSV plan created by the scan command')
    ap.add_argument('--target', help='history root where themis_history.jsonl should be updated')

    cats = sub.add_parser('categories', help='list categories already learned from approved history')
    cats.add_argument('--target', help='target root containing themis_history.jsonl')
    cats.add_argument('directories', nargs='*', help='optional scan folders used to locate history files')

    args = parser.parse_args(argv)

    if args.cmd in (None, 'gui'):
        from .gui import run_gui
        run_gui()
        return

    if args.cmd == 'scan':
        plan = build_plan(
            roots=args.directories,
            target_root=args.target,
            topics=args.topics,
            iterations=args.iterations,
            recursive=not args.no_recursive,
            include_hidden=args.include_hidden,
            min_confidence=args.min_confidence,
            bayes_threshold=args.bayes_threshold,
            min_bayes_examples=args.min_bayes_examples,
        )
        write_csv(plan, args.output)
        print(f'Created plan: {args.output}')
        _print_plan_summary(plan)

        if args.apply:
            done = apply_plan(plan, args.target)
            print(f'Applied {len(done)} move(s). Bayes history updated.')
        else:
            print('Dry run only.')
            print(f'Review/edit categories in the CSV, then run: python -m themis apply {args.output}')
        return

    if args.cmd == 'apply':
        plan = read_csv(args.plan_csv)
        done = apply_plan(plan, args.target)
        print(f'Applied {len(done)} move(s). Bayes history updated.')
        return

    if args.cmd == 'categories':
        categories = known_categories(args.target, args.directories)
        if not categories:
            print('No learned categories found yet. Apply reviewed moves first to train Bayes.')
            return
        print('Learned categories:')
        for category in categories:
            print(f'  - {category}')
        return


if __name__ == '__main__':
    main()