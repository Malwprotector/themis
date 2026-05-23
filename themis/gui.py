# gui.py
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path
from .scanner import build_plan, apply_plan, known_categories, safe_label, destination_for, infer_label_from_destination

class ThemisApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title('Themis - Guided LDA + Bayes File Sorting')
        self.geometry('1320x780')
        self.minsize(920, 620)
        self.directories=[]
        self.plan=[]
        self.categories=[]
        self.sort_column=None
        self.sort_reverse=False
        self._build()
        self._bind_shortcuts()

    def _build(self):
        self.columnconfigure(0, weight=1)
        self.rowconfigure(2, weight=1)

        header=ttk.Frame(self,padding=(12,10))
        header.grid(row=0,column=0,sticky='ew')
        header.columnconfigure(7, weight=1)

        ttk.Button(header,text='Add folder',command=self.add_directory).grid(row=0,column=0,padx=(0,6))
        ttk.Button(header,text='Clear folders',command=self.clear_directories).grid(row=0,column=1,padx=(0,16))
        ttk.Label(header,text='LDA topics').grid(row=0,column=2,sticky='e')
        self.topics=tk.IntVar(value=8)
        ttk.Spinbox(header,from_=1,to=50,width=5,textvariable=self.topics).grid(row=0,column=3,padx=(4,16))
        ttk.Label(header,text='Bayes threshold').grid(row=0,column=4,sticky='e')
        self.bayes_threshold=tk.DoubleVar(value=0.68)
        ttk.Spinbox(header,from_=0.10,to=0.99,increment=0.01,width=6,textvariable=self.bayes_threshold).grid(row=0,column=5,padx=(4,16))
        ttk.Label(header,text='Target root').grid(row=0,column=6,sticky='e')
        self.target=tk.StringVar()
        ttk.Entry(header,textvariable=self.target).grid(row=0,column=7,sticky='ew',padx=(4,6))
        ttk.Button(header,text='Browse',command=self.choose_target).grid(row=0,column=8,padx=(0,6))
        ttk.Button(header,text='Analyze',command=self.analyze).grid(row=0,column=9)

        self.dir_label=ttk.Label(self,text='Step 1: Add one or more folders, choose a target root, then click Analyze.',padding=(12,0))
        self.dir_label.grid(row=1,column=0,sticky='ew')

        main=ttk.PanedWindow(self, orient=tk.HORIZONTAL)
        main.grid(row=2,column=0,sticky='nsew',padx=12,pady=10)

        left=ttk.Frame(main)
        right=ttk.Frame(main, padding=(8,0,0,0), width=280)
        main.add(left, weight=5)
        main.add(right, weight=0)
        left.rowconfigure(0, weight=1)
        left.columnconfigure(0, weight=1)

        cols=('selected','model','category','source','destination','topic','topic_label','confidence','bayes_label','bayes_confidence','reason')
        self.tree=ttk.Treeview(left,columns=cols,show='headings',selectmode='extended')
        widths={'selected':80,'model':75,'category':160,'source':330,'destination':360,'topic':70,'topic_label':150,'confidence':90,'bayes_label':150,'bayes_confidence':120,'reason':520}
        for c in cols:
            self.tree.heading(c,text=c.replace('_',' ').title(),command=lambda col=c:self.sort_by(col))
            self.tree.column(c,width=widths.get(c,140),minwidth=60,anchor='w',stretch=False)
        vsb=ttk.Scrollbar(left,orient='vertical',command=self.tree.yview)
        hsb=ttk.Scrollbar(left,orient='horizontal',command=self.tree.xview)
        self.tree.configure(yscrollcommand=vsb.set,xscrollcommand=hsb.set)
        self.tree.grid(row=0,column=0,sticky='nsew')
        vsb.grid(row=0,column=1,sticky='ns')
        hsb.grid(row=1,column=0,sticky='ew')
        self.tree.bind('<Double-1>',lambda e:self.edit_selected_category())
        self.tree.bind('<Button-3>',self.open_context_menu)

        self._build_side_panel(right)

        bottom=ttk.Frame(self,padding=(12,0,12,12))
        bottom.grid(row=3,column=0,sticky='ew')
        bottom.columnconfigure(4, weight=1)
        ttk.Button(bottom,text='Select all',command=self.select_all_rows).grid(row=0,column=0,padx=(0,6))
        ttk.Button(bottom,text='Select none',command=self.select_no_rows).grid(row=0,column=1,padx=(0,6))
        ttk.Button(bottom,text='Toggle move flag',command=self.toggle_selected_flag).grid(row=0,column=2,padx=(0,6))
        ttk.Button(bottom,text='Edit category',command=self.edit_selected_category).grid(row=0,column=3,padx=(0,6))
        self.status=tk.StringVar(value='Ready.')
        ttk.Label(bottom,textvariable=self.status).grid(row=0,column=4,sticky='w',padx=10)
        ttk.Button(bottom,text='Apply moves + train Bayes',command=self.apply).grid(row=0,column=5,sticky='e')

    def _build_side_panel(self, panel):
        """Build a compact, scrollable help/editor panel.
        This prevents long help text from overflowing on smaller screens.
        """
        panel.grid_propagate(False)
        panel.configure(width=280)
        panel.rowconfigure(0, weight=1)
        panel.columnconfigure(0, weight=1)

        canvas=tk.Canvas(panel, borderwidth=0, highlightthickness=0, width=270)
        scrollbar=ttk.Scrollbar(panel, orient='vertical', command=canvas.yview)
        content=ttk.Frame(canvas)
        content.columnconfigure(0, weight=1)

        window_id=canvas.create_window((0,0), window=content, anchor='nw')
        canvas.configure(yscrollcommand=scrollbar.set)
        canvas.grid(row=0,column=0,sticky='nsew')
        scrollbar.grid(row=0,column=1,sticky='ns')

        def _sync_scroll_region(event=None):
            canvas.configure(scrollregion=canvas.bbox('all'))
            canvas.itemconfigure(window_id, width=canvas.winfo_width())
        content.bind('<Configure>', _sync_scroll_region)
        canvas.bind('<Configure>', _sync_scroll_region)
        canvas.bind_all('<MouseWheel>', lambda e: canvas.yview_scroll(int(-1*(e.delta/120)), 'units'))

        wrap=245
        ttk.Label(content,text='Guided workflow',font=('TkDefaultFont',10,'bold')).grid(row=0,column=0,sticky='w')
        guide=(
            '1. LDA creates initial groups when there is no training data.\n'
            '2. Review the Category column.\n'
            '3. Correct categories here or by double-clicking a row.\n'
            '4. Apply moves. Approved categories become Bayes training data.\n'
            '5. Run Analyze again: Bayes takes over when confident.'
        )
        ttk.Label(content,text=guide,wraplength=wrap,justify='left').grid(row=1,column=0,sticky='ew',pady=(4,14))

        ttk.Label(content,text='Fast category editor',font=('TkDefaultFont',10,'bold')).grid(row=2,column=0,sticky='w')
        ttk.Label(content,text='Category for selected rows',wraplength=wrap).grid(row=3,column=0,sticky='w',pady=(8,0))
        self.category_var=tk.StringVar()
        self.category_combo=ttk.Combobox(content,textvariable=self.category_var,values=self.categories)
        self.category_combo.grid(row=4,column=0,sticky='ew',pady=(2,6))
        ttk.Button(content,text='Apply category',command=self.apply_category_to_selected).grid(row=5,column=0,sticky='ew',pady=(0,6))
        ttk.Button(content,text='Use Bayes suggestion',command=self.use_bayes_for_selected).grid(row=6,column=0,sticky='ew',pady=(0,14))

        ttk.Label(content,text='Filter').grid(row=7,column=0,sticky='w')
        self.filter_var=tk.StringVar()
        self.filter_var.trace_add('write',lambda *_: self.reload_tree())
        ttk.Entry(content,textvariable=self.filter_var).grid(row=8,column=0,sticky='ew',pady=(2,14))

        ttk.Label(content,text='Shortcuts',font=('TkDefaultFont',10,'bold')).grid(row=9,column=0,sticky='w')
        shortcuts='Ctrl+A select all\nSpace toggle move flag\nEnter edit category\nCtrl+R analyze again\nCtrl+S apply moves'
        ttk.Label(content,text=shortcuts,wraplength=wrap,justify='left').grid(row=10,column=0,sticky='ew',pady=(4,0))

    def _bind_shortcuts(self):
        self.bind('<Control-a>',lambda e:(self.select_all_rows(), 'break'))
        self.bind('<space>',lambda e:(self.toggle_selected_flag(), 'break'))
        self.bind('<Return>',lambda e:(self.edit_selected_category(), 'break'))
        self.bind('<Control-r>',lambda e:(self.analyze(), 'break'))
        self.bind('<Control-s>',lambda e:(self.apply(), 'break'))

    def selected_indices(self):
        return [int(i) for i in self.tree.selection()]

    def add_directory(self):
        d=filedialog.askdirectory(title='Choose a folder to sort')
        if d and d not in self.directories:
            self.directories.append(d)
            self.refresh_dirs()

    def clear_directories(self):
        self.directories=[]
        self.refresh_dirs()

    def choose_target(self):
        d=filedialog.askdirectory(title='Choose target root')
        if d: self.target.set(d)

    def refresh_dirs(self):
        self.dir_label.config(text='Folders: '+', '.join(self.directories) if self.directories else 'Step 1: Add one or more folders, choose a target root, then click Analyze.')

    def analyze(self):
        if not self.directories:
            messagebox.showwarning('Themis','Please add at least one folder first.')
            return
        self.status.set('Analyzing with LDA + Bayes...')
        self.update_idletasks()
        self.plan=build_plan(self.directories,self.target.get() or None,self.topics.get(),bayes_threshold=self.bayes_threshold.get())
        self.categories=known_categories(self.target.get() or None,self.directories)
        for p in self.plan:
            if p.category not in self.categories:
                self.categories.append(p.category)
        self.categories=sorted(set(self.categories), key=str.lower)
        self.category_combo.configure(values=self.categories)
        self.reload_tree()
        bayes=sum(1 for p in self.plan if p.model=='bayes')
        lda=sum(1 for p in self.plan if p.model=='lda')
        self.status.set(f'Analysis complete: {len(self.plan)} files. LDA: {lda}. Bayes: {bayes}. Review categories before applying moves.')

    def row_matches_filter(self,p):
        q=self.filter_var.get().strip().lower()
        if not q: return True
        hay=' '.join([p.source,p.destination,p.category,p.model,p.reason]).lower()
        return q in hay

    def reload_tree(self):
        selected=set(self.tree.selection())
        self.tree.delete(*self.tree.get_children())
        for i,p in enumerate(self.plan):
            if not self.row_matches_filter(p):
                continue
            values=(p.selected,p.model,p.category,p.source,p.destination,p.topic,p.topic_label,p.confidence,p.bayes_label,p.bayes_confidence,p.reason)
            self.tree.insert('', 'end', iid=str(i), values=values)
        for iid in selected:
            if self.tree.exists(iid): self.tree.selection_add(iid)

    def sort_by(self,col):
        self.sort_reverse = not self.sort_reverse if self.sort_column==col else False
        self.sort_column=col
        def key(p):
            return getattr(p,col,'')
        self.plan.sort(key=key, reverse=self.sort_reverse)
        self.reload_tree()

    def select_all_rows(self):
        self.tree.selection_set(self.tree.get_children())

    def select_no_rows(self):
        self.tree.selection_remove(self.tree.selection())

    def toggle_selected_flag(self):
        idxs=self.selected_indices()
        if not idxs: return
        for i in idxs:
            self.plan[i].selected=not self.plan[i].selected
        self.reload_tree()

    def target_root(self):
        if self.target.get(): return Path(self.target.get()).expanduser().resolve()
        if self.directories: return Path(self.directories[0]).expanduser().resolve()/'Themis_Sorted'
        return Path.cwd()/'Themis_Sorted'

    def set_category(self, index, category):
        p=self.plan[index]
        category=safe_label(category)
        p.category=category
        p.destination=str(destination_for(self.target_root(), category, Path(p.source).name, prefix=None))
        p.model='manual'
        p.reason='Manual category correction. This row will train Bayes after Apply.'
        if category not in self.categories:
            self.categories.append(category)
            self.categories=sorted(set(self.categories), key=str.lower)
            self.category_combo.configure(values=self.categories)

    def apply_category_to_selected(self):
        idxs=self.selected_indices()
        category=self.category_var.get().strip()
        if not idxs:
            messagebox.showinfo('Themis','Select one or more rows first.')
            return
        if not category:
            messagebox.showinfo('Themis','Type or choose a category first.')
            return
        for i in idxs:
            self.set_category(i, category)
        self.reload_tree()
        self.status.set(f'Category "{safe_label(category)}" applied to {len(idxs)} row(s). Bayes will learn it after Apply.')

    def use_bayes_for_selected(self):
        idxs=self.selected_indices()
        changed=0
        for i in idxs:
            p=self.plan[i]
            if p.bayes_label:
                self.set_category(i, p.bayes_label)
                p.model='bayes'
                p.reason='Bayes suggestion manually accepted. This approval strengthens Bayes after Apply.'
                changed+=1
        self.reload_tree()
        self.status.set(f'Bayes suggestion applied to {changed} row(s).')

    def edit_selected_category(self):
        idxs=self.selected_indices()
        if not idxs:
            messagebox.showinfo('Themis','Select a row first.')
            return
        current=self.plan[idxs[0]].category or infer_label_from_destination(self.plan[idxs[0]].destination)
        win=tk.Toplevel(self)
        win.title('Edit category')
        win.geometry('560x210')
        win.transient(self)
        win.columnconfigure(0,weight=1)
        ttk.Label(win,text=f'Editing {len(idxs)} selected row(s). This category becomes Bayes training data after Apply.').grid(row=0,column=0,sticky='ew',padx=12,pady=(12,6))
        val=tk.StringVar(value=current)
        combo=ttk.Combobox(win,textvariable=val,values=self.categories)
        combo.grid(row=1,column=0,sticky='ew',padx=12,pady=6)
        combo.focus_set()
        def save():
            category=val.get().strip()
            if not category: return
            for i in idxs: self.set_category(i,category)
            self.reload_tree(); win.destroy()
            self.status.set(f'Category "{safe_label(category)}" applied to {len(idxs)} row(s).')
        ttk.Button(win,text='Save category',command=save).grid(row=2,column=0,pady=10)
        win.bind('<Return>',lambda e:save())

    def open_context_menu(self,event):
        iid=self.tree.identify_row(event.y)
        if iid and iid not in self.tree.selection():
            self.tree.selection_set(iid)
        menu=tk.Menu(self,tearoff=False)
        menu.add_command(label='Edit category',command=self.edit_selected_category)
        menu.add_command(label='Toggle move flag',command=self.toggle_selected_flag)
        menu.add_command(label='Use Bayes suggestion',command=self.use_bayes_for_selected)
        menu.tk_popup(event.x_root,event.y_root)

    def apply(self):
        if not self.plan:
            messagebox.showinfo('Themis','No plan to apply. Analyze a folder first.')
            return
        selected_count=sum(1 for p in self.plan if p.selected)
        if selected_count==0:
            messagebox.showinfo('Themis','No rows are marked for moving. Toggle the move flag for at least one row.')
            return
        msg=(f'Move {selected_count} selected file(s) now?\n\n'
             'Approved categories will be saved to themis_history.jsonl and used to train Bayes next time.')
        if messagebox.askyesno('Confirm moves',msg):
            done=apply_plan(self.plan,self.target.get() or None)
            messagebox.showinfo('Themis',f'Applied {len(done)} move(s). Bayes history updated.')
            self.plan=[]
            self.reload_tree()
            self.status.set('Moves applied. Click Analyze again to let Bayes use the new training data.')

def run_gui():
    ThemisApp().mainloop()
