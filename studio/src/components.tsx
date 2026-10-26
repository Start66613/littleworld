import type { ReactNode, ButtonHTMLAttributes } from "react";
import { ArrowDown, ArrowUp, Trash2, Plus, X, Search } from "lucide-react";
export function Button({
  children,
  kind = "",
  ...props
}: ButtonHTMLAttributes<HTMLButtonElement> & { kind?: string }) {
  return (
    <button type="button" className={`button ${kind}`} {...props}>
      {children}
    </button>
  );
}
export function Field({
  label,
  children,
  hint,
}: {
  label: string;
  children: ReactNode;
  hint?: string;
}) {
  return (
    <label className="field">
      <span>{label}</span>
      {children}
      {hint && <small>{hint}</small>}
    </label>
  );
}
export function Empty({
  title,
  description,
  action,
}: {
  title: string;
  description: string;
  action?: ReactNode;
}) {
  return (
    <div className="empty">
      <div className="empty-art">✿</div>
      <h3>{title}</h3>
      <p>{description}</p>
      {action}
    </div>
  );
}
export function Add({
  onClick,
  children,
  disabled,
}: {
  onClick: () => void;
  children: ReactNode;
  disabled?: boolean;
}) {
  return (
    <Button kind="soft" onClick={onClick} disabled={disabled}>
      <Plus size={16} />
      {children}
    </Button>
  );
}
export function ItemActions({
  index,
  count,
  onMove,
  onDelete,
  label = "条目",
}: {
  index: number;
  count: number;
  onMove: (a: number, b: number) => void;
  onDelete: () => void;
  label?: string;
}) {
  return (
    <div className="item-actions">
      <Button
        kind="icon"
        aria-label={`上移${label}`}
        disabled={index === 0}
        onClick={() => onMove(index, index - 1)}
      >
        <ArrowUp size={15} />
      </Button>
      <Button
        kind="icon"
        aria-label={`下移${label}`}
        disabled={index === count - 1}
        onClick={() => onMove(index, index + 1)}
      >
        <ArrowDown size={15} />
      </Button>
      <Button kind="icon danger" aria-label={`删除${label}`} onClick={onDelete}>
        <Trash2 size={15} />
      </Button>
    </div>
  );
}
export function Modal({
  title,
  children,
  onClose,
  wide = false,
}: {
  title: string;
  children: ReactNode;
  onClose: () => void;
  wide?: boolean;
}) {
  return (
    <div
      className="modal-backdrop"
      onMouseDown={(e) => {
        if (e.target === e.currentTarget) onClose();
      }}
    >
      <section
        className={`modal ${wide ? "wide" : ""}`}
        role="dialog"
        aria-modal="true"
        aria-label={title}
      >
        <header>
          <h2>{title}</h2>
          <Button kind="icon" onClick={onClose} aria-label="关闭对话框">
            <X size={20} />
          </Button>
        </header>
        {children}
      </section>
    </div>
  );
}
export function SearchBox({
  value,
  onChange,
}: {
  value: string;
  onChange: (value: string) => void;
}) {
  return (
    <label className="search">
      <Search size={17} />
      <input
        aria-label="搜索回忆"
        placeholder="搜索文字、日期或标签…"
        value={value}
        onChange={(e) => onChange(e.target.value)}
      />
    </label>
  );
}
