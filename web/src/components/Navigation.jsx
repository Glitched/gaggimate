import { useLocation } from 'preact-iso';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faHome } from '@fortawesome/free-solid-svg-icons/faHome';
import { faList } from '@fortawesome/free-solid-svg-icons/faList';
import { faTimeline } from '@fortawesome/free-solid-svg-icons/faTimeline';
import { faBluetoothB } from '@fortawesome/free-brands-svg-icons/faBluetoothB';
import { faCog } from '@fortawesome/free-solid-svg-icons/faCog';
import { faRotate } from '@fortawesome/free-solid-svg-icons/faRotate';
import { faMagnifyingGlassChart } from '@fortawesome/free-solid-svg-icons/faMagnifyingGlassChart';
import { faChartSimple } from '@fortawesome/free-solid-svg-icons/faChartSimple';
import { faCircleChevronLeft } from '@fortawesome/free-solid-svg-icons/faCircleChevronLeft';
import { faCircleChevronRight } from '@fortawesome/free-solid-svg-icons/faCircleChevronRight';
import { Wordmark } from './Wordmark.jsx';
import { faGithub } from '@fortawesome/free-brands-svg-icons/faGithub';
import { faDiscord } from '@fortawesome/free-brands-svg-icons/faDiscord';
import { useEffect, useRef } from 'preact/hooks';
import { faPencil } from '@fortawesome/free-solid-svg-icons/faPencil';
import { faCheck } from '@fortawesome/free-solid-svg-icons/faCheck';
import {
  DASHBOARD_MODES,
  dashboardModeSignal,
  setDashboardMode,
} from '../utils/dashboardManager.js';
import { machine } from '../services/ApiService.js';
import { computed } from '@preact/signals';

const update = computed(() => machine.value.status.update);

const NAVIGATION_SECTIONS = [
  {
    id: 'dashboard',
    showDivider: true,
    items: [
      {
        label: 'Dashboard',
        link: '/',
        icon: faHome,
        editLink: '/dashboard-settings',
        editIcon: faPencil,
      },
    ],
  },
  {
    id: 'analysis',
    showDivider: true,
    items: [
      { label: 'Profiles', link: '/profiles', icon: faList },
      { label: 'Shot History', link: '/history', icon: faTimeline },
      { label: 'Shot Analyzer', link: '/analyzer', icon: faMagnifyingGlassChart, isNew: false },
      { label: 'Statistics', link: '/statistics', icon: faChartSimple, isNew: false },
    ],
  },
  {
    id: 'settings',
    showDivider: true,
    items: [{ label: 'Settings', link: '/settings', icon: faCog }],
  },
];

function DashboardModeDropdown({ editLink, editIcon, editActive, isActive }) {
  const mode = dashboardModeSignal.value;

  // daisyUI dropdowns are focus-driven — blur to close after a selection.
  const selectMode = value => {
    setDashboardMode(value);
    document.activeElement?.blur();
  };

  const options = [
    { value: DASHBOARD_MODES.SIMPLE, label: 'Simple' },
    { value: DASHBOARD_MODES.ADVANCED, label: 'Advanced' },
  ];

  return (
    <div className='dropdown dropdown-end h-full'>
      {/* div+tabindex instead of <button>: Safari doesn't focus buttons on click,
          which daisyUI's focus-driven dropdown relies on.

          This is the right half of the Dashboard row, so it has to carry the same
          fill as the left half or the pill looks welded together from two parts.
          It kept the solid bg-primary the nav rows used before they moved to a
          tint, which made the pencil the loudest thing in the sidebar. When the
          row is active but you are not editing, the icon dims: the row is
          selected, this control is not. */}
      <div
        tabIndex={0}
        role='button'
        aria-label='Dashboard view'
        title='Dashboard view'
        className={`flex h-full cursor-pointer items-center justify-center rounded-r-xl px-2.5 ${
          editActive
            ? 'bg-primary/15 text-primary'
            : isActive
              ? 'bg-primary/15 text-primary/50 hover:text-primary'
              : 'text-base-content/30 hover:bg-base-content/10 hover:text-base-content bg-transparent'
        }`}
      >
        <FontAwesomeIcon icon={editIcon} className='h-3 w-3' />
      </div>
      <ul
        tabIndex={0}
        className='dropdown-content menu bg-base-100 rounded-box border-base-300 z-10 mt-1 w-44 border p-2 shadow-lg'
      >
        {options.map(option => (
          <li key={option.value}>
            <button type='button' onClick={() => selectMode(option.value)}>
              <span className='flex-grow'>{option.label}</span>
              {mode === option.value && <FontAwesomeIcon icon={faCheck} className='h-3 w-3' />}
            </button>
          </li>
        ))}
        <li>
          <a href={editLink} onClick={() => selectMode(DASHBOARD_MODES.CUSTOM)}>
            <span className='flex-grow'>Customize</span>
            {mode === DASHBOARD_MODES.CUSTOM && (
              <FontAwesomeIcon icon={faCheck} className='h-3 w-3' />
            )}
          </a>
        </li>
      </ul>
    </div>
  );
}

function MenuItem({
  collapsed = false,
  icon,
  isNew = false,
  isUnread = false,
  label,
  link,
  editLink,
  editIcon,
}) {
  const { path } = useLocation();
  const isActive = path === link;
  const editActive = path === editLink;
  const isExpanded = collapsed === false;
  const editLinkEnabled = editLink && editIcon && !collapsed;
  // Rounding lives on the children — the row wrapper can't use overflow-hidden
  // or it would clip the dashboard-mode dropdown.
  // px-3 (12px), not px-2: rounded-xl is a 12px radius, so with 8px padding the
  // icon sat inside the corner's sweep and the filled active row looked cramped.
  // 12px also puts the icon centre at 20px, matching the footer's 32px circular
  // buttons in their px-1 container -- those two columns were 4px apart before.
  // font-medium: daisyUI's .btn hardcodes font-weight 600, which is heavier than
  // this rail wants. 500 keeps the labels readable without them shouting.
  const commonClasses = `btn btn-md border-none h-12 font-medium ${editLinkEnabled ? 'rounded-l-xl rounded-r-none' : 'rounded-xl'}`;
  const baseClassName = collapsed
    ? 'btn-square min-h-0 min-w-0 bg-transparent px-0 text-base-content hover:bg-base-content/10 hover:text-base-content'
    : 'justify-start gap-3 text-base-content hover:text-base-content hover:bg-base-content/10 bg-transparent border-none px-3';
  // The old active style did three things at once: inverted text polarity
  // (dark-on-light inside a light-on-dark panel), became the lightest object on
  // the panel, and carried ~3.5x the chroma of anything around it. Any one of
  // those reads as "selected"; together they shout. A soft tint plus primary
  // text keeps the polarity of every other row.
  const activeClassName = collapsed
    ? 'btn-square min-h-0 min-w-0 bg-primary/15 px-0 text-primary hover:bg-primary/20 hover:text-primary'
    : 'justify-start gap-3 bg-primary/15 text-primary hover:bg-primary/20 hover:text-primary px-3';
  const className = `${commonClasses} ${isActive ? activeClassName : baseClassName}`;

  return (
    <div className={`flex h-12 flex-row ${collapsed ? 'w-12' : 'w-full'}`}>
      <a
        href={link}
        className={`flex-grow ${className} relative`}
        aria-label={collapsed ? label : undefined}
        aria-current={isActive ? 'page' : undefined}
        title={collapsed ? label : undefined}
      >
        <FontAwesomeIcon size='md' icon={icon} />
        {isUnread && (
          <span className='indicator-item status status-warning absolute -top-0 -right-0'></span>
        )}
        {isExpanded ? (
          <div className='indicator'>
            {isNew ? (
              <span className='indicator-item text-success pl-8 text-xs font-bold'>NEW</span>
            ) : null}
            <span>{label}</span>
          </div>
        ) : null}
      </a>
      {editLinkEnabled && (
        <DashboardModeDropdown
          editLink={editLink}
          editIcon={editIcon}
          editActive={editActive}
          isActive={isActive}
        />
      )}
    </div>
  );
}

export function Navigation({ collapsed = false, onToggleCollapsed }) {
  const loc = useLocation();

  // Track the previous route so the collapse-on-navigation effect only fires
  // when the route actually changes, not when `collapsed` flips back to false
  // (which would close the menu immediately after the user opens it on mobile).
  const previousPathRef = useRef(loc.path);

  useEffect(() => {
    const pathChanged = previousPathRef.current !== loc.path;
    previousPathRef.current = loc.path;
    // Re-check viewport width INSIDE the effect (was captured once at module
    // init, so iPad orientation changes were ignored).
    const isMdDown = typeof window !== 'undefined' && window.innerWidth < 768;
    if (pathChanged && !collapsed && isMdDown) {
      onToggleCollapsed();
    }
  }, [loc.path, collapsed, onToggleCollapsed]);

  return (
    <>
      {!collapsed && (
        <div
          className='fixed end-0 top-0 bottom-0 left-0 z-9998 cursor-pointer backdrop-blur-sm backdrop-brightness-50 md:hidden'
          onClick={onToggleCollapsed}
        />
      )}
      <aside
        className={`sidebar border-base-300 bg-base-100 fixed top-0 left-0 z-9999 flex h-screen flex-col overflow-y-auto border-r p-5 md:static landscape:static ${
          collapsed ? 'hidden md:flex md:w-[90px] landscape:flex landscape:w-[90px]' : 'w-[290px]'
        }`}
      >
        <div className='flex h-full flex-col'>
          {/* No compact mark exists yet, so when collapsed the header is omitted
              rather than filling the rail with a placeholder. */}
          {!collapsed && (
            <div className='mb-3 flex h-12 shrink-0 items-center px-3'>
              {/* Sized by height; the wordmark is ~5:1 so width follows. px-2
                  matches the nav buttons so the lockup aligns with their icons. */}
              <Wordmark className='text-base-content h-7 w-auto' />
            </div>
          )}
          {NAVIGATION_SECTIONS.map((section, index) => (
            <div key={section.id}>
              {/* Was <hr class="h-5 border-0"> -- a divider with its border
                  removed, so sections were separated by 26px of nothing against
                  6px within, a 4.3x jump with nothing to explain it. Draw the
                  line instead, so the gap is accounted for. */}
              {/* Every section sets showDivider, so the first one drew a line
                  between the wordmark and Dashboard with nothing above it to
                  separate -- which boxed Dashboard off on its own. */}
              {section.showDivider && index > 0 ? (
                <hr className='border-base-content/10 my-3 border-0 border-t' />
              ) : null}
              <div className='space-y-1'>
                {section.items.map(item => {
                  return (
                    <MenuItem
                      key={item.link}
                      collapsed={collapsed}
                      isUnread={item.link === '/settings' && update.value}
                      {...item}
                    />
                  );
                })}
              </div>
            </div>
          ))}

          <div className='flex-grow'>&nbsp;</div>

          {!collapsed && (
            <>
              {/* Left-aligned to match the nav rows and the Collapse button below;
                  this was centred while everything around it was not. */}
              <div className='flex flex-row items-center justify-start gap-1 px-1'>
                <div className='relative inline-block'>
                  <a
                    aria-label='github'
                    rel='noopener noreferrer'
                    href='https://github.com/Glitched/gaggimate'
                    target='_blank'
                    className='btn btn-sm btn-circle text-base-content hover:text-base-content hover:bg-base-content/10 border-none bg-transparent'
                  >
                    <FontAwesomeIcon icon={faGithub} className='text-lg' />
                  </a>
                </div>

                <div className='relative inline-block'>
                  <a
                    aria-label='discord'
                    rel='noopener noreferrer'
                    href='https://discord.gaggimate.eu/'
                    target='_blank'
                    className='btn btn-sm btn-circle text-base-content hover:text-base-content hover:bg-base-content/10 border-none bg-transparent'
                  >
                    <FontAwesomeIcon icon={faDiscord} className='text-lg' />
                  </a>
                </div>
              </div>
            </>
          )}

          <div>
            <button
              type='button'
              onClick={onToggleCollapsed}
              className={
                collapsed
                  ? 'btn btn-square btn-md text-base-content hover:bg-base-content/10 hover:text-base-content h-12 min-h-0 w-12 min-w-0 rounded-xl border-none bg-transparent px-0'
                  : 'btn btn-md text-base-content hover:text-base-content hover:bg-base-content/10 h-12 w-full justify-start gap-3 border-none bg-transparent px-3 font-medium'
              }
              aria-label={collapsed ? 'Expand navigation' : 'Collapse navigation'}
              title={collapsed ? 'Expand navigation' : 'Collapse navigation'}
            >
              <FontAwesomeIcon
                size='md'
                icon={collapsed ? faCircleChevronRight : faCircleChevronLeft}
              />
              {!collapsed ? (
                <div className='indicator'>
                  <span>Collapse</span>
                </div>
              ) : null}
            </button>
          </div>
        </div>
      </aside>
    </>
  );
}
