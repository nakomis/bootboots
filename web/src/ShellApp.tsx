import { ThemeProvider } from '@mui/material/styles';
import { BrowserRouter } from 'react-router';
import { BootbootsProvider } from './BootbootsContext';
import { BootbootsRoutes } from './BootbootsRoutes';
import type { ShellContext } from './shell-contract';
import { theme } from './theme';

/** The Module Federation entry point (`bootboots/App`), mounted by nakomis-shell at ShellContext.basePath. */
export default function ShellApp(shell: ShellContext) {
  return (
    <ThemeProvider theme={theme}>
      <BootbootsProvider shell={shell}>
        <BrowserRouter basename={shell.basePath}>
          <BootbootsRoutes />
        </BrowserRouter>
      </BootbootsProvider>
    </ThemeProvider>
  );
}
