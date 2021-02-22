import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from .scanner import build_plan, apply_plan

class ThemisApp(tk.Tk):
    def __init__(self):
        super().__init__(); self.title('Thémis - File Sorting Assistant'); self.geometry('1180x680')
        self.directories=[]; self.plan=[]; self._build()
    def _build(self):
        top=ttk.Frame(self,padding=10); top.pack(fill='x')
        ttk.Button(top,text='Add directory',command=self.add_directory).pack(side='left')
        ttk.Button(top,text='Clear',command=self.clear_directories).pack(side='left',padx=5)
        ttk.Label(top,text='Topics').pack(side='left',padx=(20,4)); self.topics=tk.IntVar(value=8); ttk.Spinbox(top,from_=1,to=50,width=5,textvariable=self.topics).pack(side='left')
        ttk.Label(top,text='Target root').pack(side='left',padx=(20,4)); self.target=tk.StringVar(); ttk.Entry(top,textvariable=self.target,width=45).pack(side='left')
        ttk.Button(top,text='Browse',command=self.choose_target).pack(side='left',padx=5); ttk.Button(top,text='Analyze',command=self.analyze).pack(side='right')
        self.dir_label=ttk.Label(self,text='No directory selected',padding=(10,0)); self.dir_label.pack(fill='x')
        cols=('selected','source','destination','topic','confidence','reason'); self.tree=ttk.Treeview(self,columns=cols,show='headings')
        for c in cols: self.tree.heading(c,text=c.title()); self.tree.column(c,width=120 if c in ('selected','topic','confidence') else 280,anchor='w')
        self.tree.pack(fill='both',expand=True,padx=10,pady=10); self.tree.bind('<Double-1>',lambda e:self.edit_destination())
        bottom=ttk.Frame(self,padding=10); bottom.pack(fill='x')
        ttk.Button(bottom,text='Toggle selected',command=self.toggle_selected).pack(side='left'); ttk.Button(bottom,text='Edit destination',command=self.edit_destination).pack(side='left',padx=5)
        ttk.Button(bottom,text='Apply selected moves',command=self.apply).pack(side='right')
    def add_directory(self):
        d=filedialog.askdirectory(title='Choose a directory to sort')
        if d and d not in self.directories: self.directories.append(d); self.refresh_dirs()
    def clear_directories(self): self.directories=[]; self.refresh_dirs()
    def choose_target(self):
        d=filedialog.askdirectory(title='Choose target root')
        if d: self.target.set(d)
    def refresh_dirs(self): self.dir_label.config(text='Directories: '+', '.join(self.directories) if self.directories else 'No directory selected')
    def analyze(self):
        if not self.directories: messagebox.showwarning('Thémis','Please add at least one directory.'); return
        self.plan=build_plan(self.directories,self.target.get() or None,self.topics.get()); self.reload_tree()
    def reload_tree(self):
        self.tree.delete(*self.tree.get_children())
        for i,p in enumerate(self.plan): self.tree.insert('', 'end', iid=str(i), values=(p.selected,p.source,p.destination,p.topic,p.confidence,p.reason))
    def idx(self):
        s=self.tree.selection(); return int(s[0]) if s else None
    def toggle_selected(self):
        i=self.idx()
        if i is not None: self.plan[i].selected=not self.plan[i].selected; self.reload_tree()
    def edit_destination(self):
        i=self.idx()
        if i is None: return
        win=tk.Toplevel(self); win.title('Edit destination'); win.geometry('760x120'); val=tk.StringVar(value=self.plan[i].destination)
        ttk.Entry(win,textvariable=val,width=100).pack(fill='x',padx=10,pady=10)
        ttk.Button(win,text='Save',command=lambda:(setattr(self.plan[i],'destination',val.get()),self.reload_tree(),win.destroy())).pack(pady=5)
    def apply(self):
        if not self.plan: messagebox.showinfo('Thémis','No plan to apply.'); return
        if messagebox.askyesno('Confirm','Move selected files now?'):
            done=apply_plan(self.plan,self.target.get() or None); messagebox.showinfo('Thémis',f'Applied {len(done)} moves.'); self.plan=[]; self.reload_tree()
def run_gui(): ThemisApp().mainloop()
