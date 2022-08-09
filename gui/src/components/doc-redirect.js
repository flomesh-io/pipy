import React from 'react';

import { navigate } from 'gatsby-link';
import { useLocation } from '@reach/router';

const LANGS = ['en', 'zh'];

const DocRedirect = () => {
  const loc = useLocation();

  React.useEffect(
    () => {
      window.setTimeout(
        () => {
          const preferred = navigator.language;
          const lang = LANGS.find(s => prefered.startsWith(s)) || 'en';
          navigate(
            `/docs/${lang}/${loc.pathname.substring(6)}`,
            { replace: true },
          );
        }, 0
      );
    }, []
  );

  return <div/>;
}

export default DocRedirect;
