import { toasts, dismissToast } from '../services/toast.js';

const TYPE_CLASSES = {
  success: 'alert-success',
  error: 'alert-error',
  warning: 'alert-warning',
  info: 'alert-info',
};

export function ToastHost() {
  const items = toasts.value;
  if (items.length === 0) return null;

  return (
    <div className='toast toast-end toast-bottom z-[100]'>
      {items.map(t => (
        <div
          key={t.id}
          role='alert'
          className={`alert ${TYPE_CLASSES[t.type] || TYPE_CLASSES.info} cursor-pointer shadow-lg`}
          onClick={() => dismissToast(t.id)}
        >
          <span>{t.message}</span>
        </div>
      ))}
    </div>
  );
}
