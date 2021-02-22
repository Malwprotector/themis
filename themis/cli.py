#cli
import argparse
from .scanner import build_plan, write_csv, read_csv, apply_plan

def main(argv=None):
    parser=argparse.ArgumentParser(prog='themis', description='Thémis - file sorting assistant based on filename topics')
    sub=parser.add_subparsers(dest='cmd')
    sub.add_parser('gui', help='open graphical interface')
    scan=sub.add_parser('scan', help='scan folders and create a proposed sorting plan')
    scan.add_argument('directories', nargs='+'); scan.add_argument('--target'); scan.add_argument('--topics', type=int, default=8)
    scan.add_argument('--iterations', type=int, default=350); scan.add_argument('--output', default='themis_plan.csv')
    scan.add_argument('--no-recursive', action='store_true'); scan.add_argument('--include-hidden', action='store_true'); scan.add_argument('--apply', action='store_true')
    ap=sub.add_parser('apply', help='apply a reviewed CSV plan'); ap.add_argument('plan_csv')
    args=parser.parse_args(argv)
    if args.cmd in (None,'gui'):
        from .gui import run_gui; run_gui(); return
    if args.cmd=='scan':
        plan=build_plan(args.directories,args.target,args.topics,args.iterations,not args.no_recursive,args.include_hidden)
        write_csv(plan,args.output); print(f'Created plan: {args.output} ({len(plan)} files).')
        if args.apply: print(f'Applied {len(apply_plan(plan,args.target))} moves.')
        else: print('Dry run only. Review/edit the CSV, then run: python -m themis apply '+args.output)
    if args.cmd=='apply':
        print(f'Applied {len(apply_plan(read_csv(args.plan_csv)))} moves.')
if __name__=='__main__': main()
